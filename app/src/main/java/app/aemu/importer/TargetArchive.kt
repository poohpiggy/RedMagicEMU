package app.aemu.importer

import org.apache.commons.compress.archivers.zip.ZipArchiveEntry
import org.apache.commons.compress.archivers.zip.ZipFile
import java.io.File
import java.io.IOException
import java.nio.file.Files
import java.util.zip.CRC32

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

    fun validate(file: File, expectedEntries: Map<String, Long>? = null) {
        inspect(file, expectedEntries)
    }

    private fun inspect(file: File, expectedEntries: Map<String, Long>?): Map<String, Long> {
        val limits = expectedEntries ?: entryLimits
        val found = mutableMapOf<String, Long>()
        ZipFile.builder().setFile(file).get().use { zip ->
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
        }
        return found
    }

    /** Extract selected fixed-name entries from a verified private snapshot, checking every byte. */
    fun <T> withExtracted(
        file: File,
        parent: File,
        expectedEntries: Map<String, Long>? = null,
        names: Set<String>,
        cancelled: () -> Boolean,
        optionalNames: Set<String> = emptySet(),
        availableBytes: (File) -> Long = { it.usableSpace },
        block: (Map<String, File>) -> T,
    ): T {
        if (!parent.isDirectory && !parent.mkdirs()) throw IOException("Cannot create extraction directory")
        val sizes = inspect(file, expectedEntries)
        if (names.isEmpty() || !sizes.keys.containsAll(names) || names.any { !it.matches(Regex("[A-Za-z0-9_.-]+")) || it == "." || it == ".." })
            throw IOException("Invalid extraction entry name")
        if (optionalNames.any { it != ZTECFG } || optionalNames.any { it in names }) throw IOException("Invalid optional extraction entry")
        val selected = names + optionalNames.filter { it in sizes }
        var required = 0L
        for (name in selected) {
            val size = sizes.getValue(name)
            if (size < 0 || size > Long.MAX_VALUE - required) throw IOException("Invalid extraction size")
            required += size
        }
        if (availableBytes(parent) < required) throw IOException("Not enough space to extract firmware")
        val dir = Files.createTempDirectory(parent.toPath(), "firmware-extract-").toFile()
        var failure: Throwable? = null
        try {
            val extracted = mutableMapOf<String, File>()
            ZipFile.builder().setFile(file).get().use { zip ->
                for (name in selected.sorted()) {
                    if (cancelled()) throw ImportCancelledException()
                    val entry = zip.getEntry(name) ?: throw IOException("Missing firmware entry: $name")
                    val output = File(dir, name)
                    val crc = CRC32()
                    var count = 0L
                    zip.getInputStream(entry).use { input ->
                        output.outputStream().buffered(1024 * 1024).use { out ->
                            val buf = ByteArray(1024 * 1024)
                            while (true) {
                                if (cancelled()) throw ImportCancelledException()
                                val n = input.read(buf)
                                if (n < 0) break
                                if (n == 0) continue
                                if (n.toLong() > entry.size - count) throw IOException("Firmware entry exceeds declared size: $name")
                                out.write(buf, 0, n)
                                crc.update(buf, 0, n)
                                count += n
                            }
                        }
                    }
                    if (count != entry.size || crc.value != entry.crc) throw IOException("Firmware entry is corrupt: $name")
                    extracted[name] = output
                }
            }
            if (cancelled()) throw ImportCancelledException()
            return block(extracted)
        } catch (t: Throwable) {
            failure = t
            throw t
        } finally {
            if (!dir.deleteRecursively()) {
                val cleanup = IOException("Cannot remove incomplete firmware extraction")
                if (failure != null) failure.addSuppressed(cleanup) else throw cleanup
            }
        }
    }

}
