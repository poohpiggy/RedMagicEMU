package app.aemu.importer

import org.apache.commons.compress.archivers.zip.ZipArchiveEntry
import org.apache.commons.compress.archivers.zip.ZipFile
import java.io.ByteArrayInputStream
import java.io.EOFException
import java.io.File
import java.io.IOException
import java.io.InputStream
import java.io.InterruptedIOException
import java.io.SequenceInputStream
import java.nio.file.Files
import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionException
import java.util.concurrent.Executors
import java.util.zip.CRC32
import java.util.zip.Inflater
import java.util.zip.InflaterInputStream

/** Bounded ZIP format used by the target firmware bundle. */
internal object TargetArchive {
    const val MAX_PACKAGE_SIZE = 32L * 1024 * 1024 * 1024
    private val entryLimits = mapOf(
        "ramdisk.img" to 64L * 1024 * 1024,
        "super.img" to 32L * 1024 * 1024 * 1024,
        "boot.img" to 256L * 1024 * 1024,
        "init_boot.img" to 256L * 1024 * 1024,
        "vendor_boot.img" to 256L * 1024 * 1024,
        "README.txt" to 1024L * 1024,
    )
    private const val ZTECFG = "ztecfg.img"
    private const val ZTECFG_SIZE = 512L * 1024
    private const val DEFLATE_INPUT_BUFFER = 2 * 1024 * 1024

    fun validate(file: File, expectedEntries: Map<String, Long>? = null) {
        ZipFile.builder().setFile(file).get().use { inspect(it, expectedEntries) }
    }

    private fun inspect(zip: ZipFile, expectedEntries: Map<String, Long>?): Map<String, Long> {
        val limits = expectedEntries ?: entryLimits
        val found = mutableMapOf<String, Long>()
        val entries = zip.entries
        while (entries.hasMoreElements()) {
            val e = entries.nextElement()
            val limit = limits[e.name] ?: if (expectedEntries == null && e.name == ZTECFG) ZTECFG_SIZE
                else throw IOException("Unexpected firmware ZIP entry: ${e.name}")
            val type = e.unixMode and 0xF000
            if (found.containsKey(e.name) || e.isDirectory || e.isUnixSymlink || type != 0 && type != 0x8000 ||
                e.size < 0 || (if (expectedEntries == null && e.name != ZTECFG) e.size > limit else e.size != limit) ||
                e.compressedSize < 0 || e.method !in listOf(ZipArchiveEntry.STORED, ZipArchiveEntry.DEFLATED) ||
                !zip.canReadEntryData(e)) throw IOException("Invalid firmware ZIP entry: ${e.name}")
            found[e.name] = e.size
        }
        if (!found.keys.containsAll(limits.keys)) throw IOException("Firmware ZIP entries are missing")
        return found
    }

    fun <T> withExtracted(
        file: File,
        parent: File,
        expectedEntries: Map<String, Long>? = null,
        names: Set<String>,
        cancelled: () -> Boolean,
        optionalNames: Set<String> = emptySet(),
        availableBytes: (File) -> Long = { it.usableSpace },
        block: (Map<String, File>) -> T,
    ): T = withExtracted(ZipFile.builder().setFile(file), parent, expectedEntries, names, cancelled,
        optionalNames, availableBytes, block = block)

    /** Validate and extract from the same ZIP reader. Only verified private files reach the consumer. */
    fun <T> withExtracted(
        archive: ZipFile.Builder,
        parent: File,
        expectedEntries: Map<String, Long>? = null,
        names: Set<String>,
        cancelled: () -> Boolean,
        optionalNames: Set<String> = emptySet(),
        availableBytes: (File) -> Long = { it.usableSpace },
        onBytesExtracted: (Long, Long) -> Unit = { _, _ -> },
        prepareTree: PendingPackageTree? = null,
        block: (Map<String, File>) -> T,
    ): T {
        if (!parent.isDirectory && !parent.mkdirs()) throw IOException("Cannot create extraction directory")
        var dir: File? = null
        var failure: Throwable? = null
        try {
            if (cancelled()) throw ImportCancelledException()
            val extracted = archive.get().use { zip ->
                val sizes = inspect(zip, expectedEntries)
                if (names.isEmpty() || !sizes.keys.containsAll(names) || names.any { !it.matches(Regex("[A-Za-z0-9_.-]+")) || it == "." || it == ".." })
                    throw IOException("Invalid extraction entry name")
                if (optionalNames.any { it != ZTECFG } || optionalNames.any { it in names }) throw IOException("Invalid optional extraction entry")
                if (prepareTree != null && ("super.img" !in names || "ramdisk.img" !in names))
                    throw IOException("Tree extraction requires super.img and ramdisk.img")
                val selected = names + optionalNames.filter { it in sizes }
                var required = 0L
                for (name in selected) {
                    val size = sizes.getValue(name)
                    if (size < 0 || size > Long.MAX_VALUE - required) throw IOException("Invalid extraction size")
                    required += size
                }
                if (availableBytes(parent) < required) throw IOException("Not enough space to extract firmware")
                val scratch = Files.createTempDirectory(parent.toPath(), "firmware-extract-").toFile().also { dir = it }
                val files = mutableMapOf<String, File>()
                val buf = ByteArray(1024 * 1024)
                var completed = 0L
                onBytesExtracted(0L, required)
                for (name in selected.sorted()) {
                    if (cancelled()) throw ImportCancelledException()
                    val entry = zip.getEntry(name) ?: throw IOException("Missing firmware entry: $name")
                    val output = File(scratch, name)
                    entryStream(zip, entry).use { input ->
                        val progress: (Long) -> Unit = { onBytesExtracted(completed + it, required) }
                        if (name == "super.img") {
                            val path = output.toPath()
                            val onPrefix = prepareTree?.let { tree ->
                                val ramdisk = files.getValue("ramdisk.img")
                                val publish: (Long) -> Unit = { end -> tree.prefixAvailable(path, entry.size, ramdisk, end) }
                                publish
                            }
                            SuperImageExtraction.write(input, path, entry.size, entry.crc, cancelled, progress, onPrefix)
                        } else {
                            extractEntry(input, output, entry, buf, cancelled, progress)
                        }
                    }
                    completed += entry.size
                    files[name] = output
                }
                files
            }
            if (cancelled()) throw ImportCancelledException()
            prepareTree?.markVerified()
            return block(extracted)
        } catch (t: Throwable) {
            failure = t
            throw t
        } finally {
            prepareTree?.close()
            if (dir?.deleteRecursively() == false) {
                val cleanup = IOException("Cannot remove incomplete firmware extraction")
                if (failure != null) failure.addSuppressed(cleanup) else throw cleanup
            }
        }
    }

    private fun extractEntry(
        input: InputStream, output: File, entry: ZipArchiveEntry, buf: ByteArray,
        cancelled: () -> Boolean, onBytesWritten: (Long) -> Unit,
    ) {
        val crc = CRC32()
        var count = 0L
        output.outputStream().buffered().use { out ->
            while (true) {
                if (cancelled()) throw ImportCancelledException()
                val n = input.read(buf)
                if (n < 0) break
                if (n == 0) continue
                if (n.toLong() > entry.size - count) throw IOException("Firmware entry exceeds declared size: ${entry.name}")
                out.write(buf, 0, n)
                crc.update(buf, 0, n)
                count += n
                onBytesWritten(count)
            }
        }
        if (count != entry.size || crc.value != entry.crc) throw IOException("Firmware entry is corrupt: ${entry.name}")
    }

    private fun entryStream(zip: ZipFile, entry: ZipArchiveEntry): InputStream {
        if (entry.name != "super.img" || entry.method != ZipArchiveEntry.DEFLATED)
            return zip.getInputStream(entry)
        if (!zip.canReadEntryData(entry)) throw IOException("Unsupported firmware ZIP entry: ${entry.name}")
        val raw = zip.getRawInputStream(entry) ?: throw IOException("Missing compressed firmware entry")
        return inflateSuper(raw)
    }

    internal fun inflateSuper(raw: InputStream): InputStream {
        // A raw ZIP deflate stream may need a trailing padding byte when using nowrap.
        val input = SequenceInputStream(raw, ByteArrayInputStream(byteArrayOf(0)))
        return object : InflaterInputStream(input, Inflater(true), DEFLATE_INPUT_BUFFER) {
            private var nextBuffer = ByteArray(DEFLATE_INPUT_BUFFER)
            private val reader = Executors.newSingleThreadExecutor { task -> Thread(task, "firmware-zip-reader") }
            private var pending: CompletableFuture<Int>? = readNext(nextBuffer)

            private fun readNext(buffer: ByteArray): CompletableFuture<Int> =
                CompletableFuture.supplyAsync({ input.read(buffer) }, reader)

            private fun takeRead(): Int {
                val read = pending ?: return -1
                pending = null
                return try {
                    read.join()
                } catch (e: CompletionException) {
                    throw e.cause ?: e
                }
            }

            override fun fill() {
                if (Thread.currentThread().isInterrupted) throw InterruptedIOException("Firmware extraction interrupted")
                len = takeRead()
                if (len < 0) throw EOFException("Truncated firmware DEFLATE stream")
                val ready = nextBuffer
                // InflaterInputStream asks for more input only after consuming its current buffer.
                nextBuffer = buf
                buf = ready
                inf.setInput(buf, 0, len)
                pending = readNext(nextBuffer)
            }

            override fun close() {
                try {
                    // Wait for the last read before closing this stream or reusing the ZIP channel.
                    takeRead()
                } finally {
                    reader.shutdown()
                    try {
                        super.close()
                    } finally {
                        inf.end()
                    }
                }
            }
        }
    }
}
