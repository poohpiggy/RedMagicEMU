package app.aemu.importer

import android.content.Context
import android.net.Uri
import android.system.Os
import app.aemu.core.GuestImage
import app.aemu.core.ImageStore
import app.aemu.core.TreeFixer
import app.aemu.core.VmPaths
import app.aemu.core.RecoveryImage
import app.aemu.core.InstanceAdmission
import org.apache.commons.compress.archivers.tar.TarArchiveEntry
import org.apache.commons.compress.archivers.tar.TarArchiveInputStream
import org.apache.commons.compress.archivers.sevenz.SevenZFile
import org.apache.commons.compress.archivers.zip.ZipArchiveEntry
import org.apache.commons.compress.archivers.zip.ZipFile
import org.apache.commons.compress.compressors.bzip2.BZip2CompressorInputStream
import org.brotli.dec.BrotliInputStream
import org.tukaani.xz.XZInputStream
import java.io.BufferedInputStream
import java.io.File
import java.io.FileInputStream
import java.io.IOException
import java.io.InputStream
import java.io.RandomAccessFile
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.channels.FileChannel
import java.nio.file.StandardOpenOption
import java.util.zip.GZIPInputStream

/**
 * Импорт прошивки в дерево гостя. Понимает (в том числе вложенные друг в друга):
 *  - ZIP для CWM/TWRP (MIUI, CyanogenMod, большинство прошивок 2.x–4.x): system/… + boot.img + updater-script
 *  - ZIP/TGZ factory-образов Google, прошивки с system.img внутри
 *  - TAR / TAR.MD5 (Samsung Odin, TouchWiz), TWRP-бэкапы (.win), tar.gz/xz/bz2
 *  - system.img: ext2/3/4, в том числе sparse (и system.img_sparsechunk.* Motorola)
 *  - system.new.dat(.br) + system.transfer.list (OTA 5.x–6.x)
 *  - уже готовое дерево rootfs в tar.gz (например, из стендов HTC)
 */
class Importer(
    private val ctx: Context,
    private val onProgress: (String, Float) -> Unit,
    private val log: (String) -> Unit,
) {
    private lateinit var paths: VmPaths
    private lateinit var root: File
    private val tmp = File(ctx.cacheDir, "import").apply { mkdirs() }
    private var ramdisk: List<BootImage.CpioEntry>? = null
    private var recovery: ByteArray? = null
    private val symlinks = ArrayList<Pair<String, String>>() // (цель, путь ссылки в госте)
    private val perms = ArrayList<Triple<String, Int, Boolean>>() // (путь, режим, рекурсивно-файлы)
    private var files = 0
    private var bytes = 0L
    private var gotSystem = false
    private val publicationGate = PublicationGate()
    var cancelled: Boolean
        get() = publicationGate.cancelled
        set(value) { publicationGate.setCancelled(value) }
    /** a path another process can open for the file being imported (payload-dumper-go reads it), null if none */
    private var topInput: String? = null

    private fun inputPath(fd: Int): String? = runCatching {
        val real = Os.readlink("/proc/self/fd/$fd")
        if (real.startsWith("/") && File(real).canRead()) real else "/proc/${Os.getpid()}/fd/$fd"
    }.getOrNull()

    fun import(uri: Uri, name: String): GuestImage {
        val images = ImageStore.imagesDir(ctx)
        PendingImports.cleanAbandoned(images)
        val required = 512L * 1024 * 1024
        if (images.usableSpace < required) throw IOException("Not enough free space to stage the target firmware")
        val lease = PendingImports.begin(images)
        var published = false
        try {
            onProgress("Staging firmware", 0f)
            val input = ctx.contentResolver.openInputStream(uri) ?: throw IOException("file failed to open")
            val result = input.use { source ->
                StagedInput.use(lease.dir, source, { cancelled }, maxBytes = TargetArchive.MAX_PACKAGE_SIZE) { staged ->
                    onProgress("Checking firmware package", -1f)
                    TargetArchive.validate(staged)
                    importVerifiedSnapshot(staged, name, lease.dir)
                }
            }
            if (cancelled) throw ImportCancelledException()
            lease.commit(result.id, publish = publicationGate::publish)
            published = true
            var finalResult = result
            try { lease.close() } catch (cleanup: Throwable) {
                val warning = "Firmware imported, but temporary control directory could not be removed: ${cleanup.message}"
                log(warning)
                finalResult = result.copy(warnings = result.warnings + warning)
            }
            onProgress("Done", 1f)
            return finalResult
        } catch (t: Throwable) {
            if (!published) try { lease.close() }
            catch (cleanup: Throwable) { throw IOException("Import failed and incomplete instance cleanup failed: ${cleanup.message}", t).apply { addSuppressed(cleanup) } }
            if (t.suppressed.isNotEmpty()) throw IOException("Import failed; temporary data cleanup also failed: ${t.suppressed.first().message}", t)
            throw t
        }
    }

    /** Parse and verify the same private snapshot that is used for extraction. */
    private fun importVerifiedSnapshot(staged: File, name: String, pending: File): GuestImage {
        return TargetArchive.withExtracted(staged, pending, names = setOf("super.img", "ramdisk.img"), cancelled = { cancelled }) { extracted ->
            if (cancelled) throw ImportCancelledException()
            val id = ImageStore.newId(ctx)
            paths = VmPaths(ctx, id, File(pending, id))
            if (!paths.dir.mkdir()) throw IOException("Cannot create a private instance")
            root = paths.root
            if (!root.mkdir()) throw IOException("Cannot create the firmware tree")
            onProgress("Extracting firmware", -1f)
            ramdisk = TargetPackageTree.extract(extracted.getValue("super.img"), extracted.getValue("ramdisk.img"), root) { cancelled }
            if (cancelled) throw ImportCancelledException()
            val bootFingerprint = TargetMetadata.readFingerprint(ramdisk.orEmpty())
            log("Firmware bootimage fingerprint: ${bootFingerprint ?: "unavailable"}; target system fingerprint: ${TargetMetadata.HISTORY_FINGERPRINT}")
            onProgress("Checking firmware fingerprint", -1f)
            val fingerprint = InstanceAdmission.readFingerprint(paths.dir, listOfNotNull(bootFingerprint))
            InstanceAdmission.requireFingerprint(fingerprint)
            if (cancelled) throw ImportCancelledException()
            onProgress("Analyzing firmware", -1f)
            val analyzed = Analyzer(ctx, paths, ramdisk).analyze(id, name)
            val img = analyzed.copy(fingerprint = fingerprint, settings = analyzed.settings.withNx809jDisplay())
            TreeFixer(ctx, paths, img, log).sanitize()
            if (cancelled) throw ImportCancelledException()
            val result = img.copy(sizeBytes = ImageStore.du(paths.dir))
            if (cancelled) throw ImportCancelledException()
            ImageStore.saveAt(paths.dir, result)
            result
        }
    }

    // ------------------------------------------------------------------ разбор контейнеров

    private fun handle(src: RandomSource, ch: FileChannel?, name: String, depth: Int) {
        if (depth > 4) return
        val head = ByteBuffer.allocate(1100)
        src.read(0, head); head.flip()
        val h = ByteArray(head.remaining()).also { head.get(it) }
        when {
            h.size > 4 && h[0] == 'P'.code.toByte() && h[1] == 'K'.code.toByte() && h[2].toInt() == 3 && h[3].toInt() == 4 -> {
                if (ch != null) importZip(ch, name, depth) else throw IOException("zip without random access")
            }
            h.size > 6 && h[0] == '7'.code.toByte() && h[1] == 'z'.code.toByte() && h[2] == 0xBC.toByte() && h[3] == 0xAF.toByte() -> {
                if (ch != null) importSevenZ(ch, name, depth) else throw IOException("7z without random access")
            }
            h.size > 7 && String(h, 0, 6, Charsets.ISO_8859_1) == "Rar!\u001a\u0007" -> {
                if (h[6].toInt() == 1) importRar5(src, name, depth) else importRar(src, name, depth)
            }
            PacFile.probe(h) -> if (ch != null) importPac(src, ch) else throw IOException("pac without random access")
            h.size > 16 && String(h, 0, 16, Charsets.ISO_8859_1) == "Multi-Interface\u0000" ->
                if (ch != null) importSbf(src, ch) else throw IOException("sbf without random access")
            PayloadReader.probe(h) -> importPayload(src, if (depth == 0) topInput else null)
            SinImage.probe(h) || SinImage.probeOld(h) && name.endsWith(".sin", true) -> importSin(streamOf(src), name.substringAfterLast('/'))
            SparseSource.probe(src) || Ext4Reader.probe(src) || SuperImage.probe(src) || ErofsReader.probe(src) -> importImage(src, "system")
            // (the content first: forum rootfs ".tar" files are gzip-compressed)
            isTar(h) || !isCompressed(h) && name.endsWith(".tar", true) || name.endsWith(".md5", true) || name.endsWith(".win", true) ->
                importTarStream(streamOf(src), name, depth, src)
            h[0] == 0x1f.toByte() && h[1] == 0x8b.toByte() -> importCompressed(GZIPInputStream(streamOf(src), 1 shl 16), name, depth)
            h[0] == 0xfd.toByte() && h[1] == '7'.code.toByte() -> importCompressed(XZInputStream(streamOf(src)), name, depth)
            h[0] == 'B'.code.toByte() && h[1] == 'Z'.code.toByte() && h[2] == 'h'.code.toByte() -> importCompressed(BZip2CompressorInputStream(streamOf(src)), name, depth)
            h[0] == 0x04.toByte() && h[1] == 0x22.toByte() && h[2] == 0x4d.toByte() && h[3] == 0x18.toByte() ->
                importCompressed(org.apache.commons.compress.compressors.lz4.FramedLZ4CompressorInputStream(streamOf(src)), name, depth)
            String(h, 0, 8, Charsets.ISO_8859_1) == "ANDROID!" -> takeBoot(readAllFrom(src))
            // raw images without a recognisable header at 0 (YAFFS2 dumps, Samsung RFS)
            Yaffs2Image.probe(src) || FatImage.probe(src) -> importImage(src, "system")
            else -> throw IOException("unknown file format \"$name\"")
        }
    }

    /**
     * gzip/xz/bzip2/lz4 around something: a tar is unpacked as a stream; anything else (GSI system.img.xz, a zip,
     * a sparse image) is written out and recognised again.
     */
    private fun importCompressed(s: InputStream, name: String, depth: Int) {
        val b = java.io.BufferedInputStream(s, 1 shl 16)
        b.mark(1024)
        val head = ByteArray(512)
        var n = 0
        while (n < head.size) { val k = b.read(head, n, head.size - n); if (k < 0) break; n += k }
        b.reset()
        if (n >= 262 && isTar(head)) { importTarStream(b, name, depth); return }
        val inner = name.replace(Regex("(?i)\\.(gz|tgz|xz|bz2|lz4)$"), "")
        val t = spill(b, inner.substringAfterLast('/'))
        try { FileChannel.open(t.toPath(), StandardOpenOption.READ).use { c -> handle(ChannelSource(c), c, inner, depth + 1) } }
        finally { t.delete() }
    }

    /** RAR 5: junrar cannot read it, the bundled 7-Zip can; a nested or unseekable one is copied out first */
    private fun importRar5(src: RandomSource, name: String, depth: Int) {
        var whole: File? = null
        val input = (if (depth == 0) topInput else null) ?: File(tmp, "${System.nanoTime()}-whole.rar").also { f ->
            whole = f; streamOf(src).use { i -> f.outputStream().use { o -> i.copyTo(o, 1 shl 20) } }
        }.absolutePath
        try {
            val dir = try { extractWith7z(input, name) } catch (e: IOException) { throw IOException("RAR5 \"$name\": 7-Zip failed (${e.message})") }
            try { importExtracted(dir, name, depth) } finally { dir.deleteRecursively() }
        } finally { whole?.delete() }
    }

    /** RAR 4 (junrar): entries handled like those of a zip, via temporary files. */
    private fun importRar(src: RandomSource, name: String, depth: Int) {
        val whole = File(tmp, "${System.nanoTime()}-whole.rar")
        streamOf(src).use { i -> whole.outputStream().use { o -> i.copyTo(o, 1 shl 20) } }
        try {
            com.github.junrar.Archive(whole).use { a ->
                val nested = ArrayList<Pair<File, String>>()
                for (fh in a.fileHeaders) {
                    if (cancelled) throw ImportCancelledException()
                    if (fh.isDirectory) continue
                    val n = fh.fileName.replace('\\', '/')
                    val base = n.substringAfterLast('/')
                    when {
                        n.startsWith("system/") -> { a.getInputStream(fh).use { writeFile(n, it, null) }; gotSystem = true }
                        base.equals("boot.img", true) -> a.getInputStream(fh).use { takeBoot(it.readBytes()) }
                        base.equals("recovery.img", true) && fh.fullUnpackSize < 64_000_000 -> a.getInputStream(fh).use { recovery = it.readBytes() }
                        base.matches(SYSTEM_IMAGE) || base.matches(Regex("(?i).*\\.(zip|7z|tar|md5|tgz|gz|xz|pac|ftf|sin)")) && fh.fullUnpackSize > 20_000_000 ->
                            nested.add(a.getInputStream(fh).use { spill(it, base) } to base)
                    }
                    onProgress("Extracting: $base", -1f)
                }
                for ((f, base) in nested) {
                    try { FileChannel.open(f.toPath(), StandardOpenOption.READ).use { c -> handle(ChannelSource(c), c, base, depth + 1) } }
                    finally { f.delete() }
                }
            }
        } finally { whole.delete() }
        if (!gotSystem) log("no system found in archive $name")
    }

    private fun isTar(h: ByteArray) = h.size > 262 && String(h, 257, 5, Charsets.ISO_8859_1) == "ustar"

    /** gzip, xz, bzip2 */
    private fun isCompressed(h: ByteArray) = h.size > 2 && (h[0] == 0x1f.toByte() && h[1] == 0x8b.toByte() ||
        h[0] == 0xfd.toByte() && h[1] == '7'.code.toByte() || h[0] == 'B'.code.toByte() && h[1] == 'Z'.code.toByte() && h[2] == 'h'.code.toByte())

    private fun streamOf(src: RandomSource): InputStream = object : InputStream() {
        var pos = 0L
        override fun read(): Int { val b = ByteArray(1); return if (read(b, 0, 1) <= 0) -1 else b[0].toInt() and 0xff }
        override fun read(b: ByteArray, off: Int, len: Int): Int {
            if (pos >= src.size) return -1
            val k = minOf(len.toLong(), src.size - pos).toInt()
            src.read(pos, ByteBuffer.wrap(b, off, k))
            pos += k
            return k
        }
    }.let { BufferedInputStream(it, 1 shl 20) }

    private fun readAllFrom(src: RandomSource): ByteArray {
        val b = ByteBuffer.allocate(src.size.toInt()); src.read(0, b); return b.array()
    }

    private var zipChannel: FileChannel? = null
    /** partition images spilled out of a nested zip, imported only after that zip's temp file is gone (disk peak) */
    private var deferred: MutableList<Pair<File, String>>? = null

    private fun importZip(ch: FileChannel, name: String, depth: Int) {
        val zip = ZipFile.builder().setSeekableByteChannel(ch).get()
        val prevCh = zipChannel
        zipChannel = ch
        try { importZipEntries(zip, name, depth) } finally { zipChannel = prevCh }
    }

    private fun importZipEntries(zip: ZipFile, name: String, depth: Int) {
        val entries = zip.entries.toList()
        val total = entries.sumOf { maxOf(0L, it.size) }.coerceAtLeast(1)
        var done = 0L
        // сначала — сценарий установки (ссылки и права)
        entries.firstOrNull { it.name.endsWith("META-INF/com/google/android/updater-script") }?.let { e ->
            zip.getInputStream(e).use { parseUpdaterScript(String(it.readBytes())) }
        }
        // прошивка, упакованная вместе с папкой (Имя/META-INF/…, Имя/system/…): папку-обёртку снимаем
        val wrap = entries.firstOrNull { it.name.endsWith("META-INF/com/google/android/updater-script") }
            ?.name?.substringBefore("META-INF/")?.takeIf { it.isNotEmpty() && it.count { c -> c == '/' } == 1 } ?: ""
        if (wrap.isNotEmpty()) log("archive with wrapper folder \"${wrap.trimEnd('/')}\"")
        val names = entries.map { it.name.removePrefix(wrap) }.toSet()
        val datBr = entries.firstOrNull { it.name.matches(Regex("(.*/)?system\\.new\\.dat(\\.br)?")) }
        val sparseChunks = entries.filter { it.name.matches(Regex("(.*/)?system\\.img_sparsechunk\\.\\d+")) }.sortedBy { it.name.substringAfterLast('.').toInt() }
        for (e in entries) {
            if (cancelled) throw ImportCancelledException()
            val n = e.name.replace('\\', '/').removePrefix(wrap)
            val base = n.substringAfterLast('/')
            when {
                e.isDirectory -> {}
                n.contains("__MACOSX/") || base.startsWith("._") -> {}
                n.startsWith("system/") -> {
                    zip.getInputStream(e).use { writeFile(n, it, e.unixMode.takeIf { m -> m != 0 }) }
                    gotSystem = true
                }
                base.equals("boot.img", true) -> zip.getInputStream(e).use { takeBoot(it.readBytes()) }
                // SDK emulator images (0.9 beta, M3/M5, 1.x): ramdisk.img is a bare gzip cpio
                base.equals("ramdisk.img", true) && e.size < 16_000_000 -> zip.getInputStream(e).use { takeBoot(it.readBytes()) }
                base.equals("recovery.img", true) && e.size < 64_000_000 -> zip.getInputStream(e).use { recovery = it.readBytes() }
                base.matches(SYSTEM_IMAGE) ->
                    withEntrySource(zip, e) { importImage(it, "system") }
                // a TWRP backup zipped up (TWRP/BACKUPS/<serial>/<name>/): system.ext4.win is a tar of the partition's
                // files (gzip when compression was on), boot.emmc.win the raw boot partition
                base.matches(Regex("(?i)system\\.(ext[234]|yaffs2|f2fs)\\.win")) && !gotSystem -> zip.getInputStream(e).use { s ->
                    val b = java.io.BufferedInputStream(s, 1 shl 16)
                    b.mark(2); val gz = b.read() == 0x1f && b.read() == 0x8b; b.reset()
                    importTarStream(if (gz) GZIPInputStream(b, 1 shl 16) else b, base, depth + 1)
                }
                base.matches(Regex("(?i)boot\\.emmc\\.win")) && e.size < 64_000_000 -> zip.getInputStream(e).use { takeBoot(it.readBytes()) }
                base.equals("payload.bin", true) -> withEntrySource(zip, e) { importPayload(it, if (depth == 0 && !n.contains('/')) topInput else null) }
                // Sony .ftf: system.sin is the system partition, kernel.sin the kernel with the ramdisk
                base.matches(Regex("(?i)(system|kernel|boot)\\.sin")) && !(gotSystem && base.startsWith("system", true)) ->
                    zip.getInputStream(e).use { importSin(it, base) }
                base.matches(Regex("(?i)vendor(\\.ext4)?\\.img")) ->
                    withEntrySource(zip, e) { runCatching { importImage(if (SparseSource.probe(it)) SparseSource(listOf(it)) else it, "vendor") } }
                // a zip around a whole firmware: image-*.zip of Google, a large zip, or a Sony .ftf (a zip itself)
                (base.lowercase().endsWith(".zip") && (base.startsWith("image-") || depth == 0 && e.size > 50_000_000) ||
                    base.lowercase().endsWith(".ftf") || base.lowercase().endsWith(".sbf")) -> {   // (Motorola's zips hold one .sbf)
                    handleNestedZip(spillEntry(zip, e), base, depth)
                }
                base.matches(Regex("(?i).*\\.(tar|tar\\.md5|md5|tgz|tar\\.gz)")) 
                    // Odin: AP/PDA/CODE — система, BL/KERNEL/HOME — ядро с рамдиском; модем и CSC не нужны
                    && !base.startsWith("MODEM") && !base.startsWith("CP_") && !base.contains("CSC") -> {
                    zip.getInputStream(e).use { s ->
                        val st = if (base.endsWith("gz")) GZIPInputStream(s, 1 shl 16) else s
                        importTarStream(st, base, depth + 1)
                    }
                }
            }
            done += maxOf(0L, e.size)
            onProgress("Extracting: $base", 0.9f * done / total)
        }
        if (datBr != null && !gotSystem) {
            val listName = datBr.name.substringBeforeLast("system.new.dat") + "system.transfer.list"
            val listE = entries.firstOrNull { it.name == listName } ?: throw IOException("system.transfer.list missing")
            val list = zip.getInputStream(listE).use { String(it.readBytes()) }
            onProgress("Building system.img from OTA", 0.5f)
            val raw = File(tmp, "system.raw")
            zip.getInputStream(datBr).use { s ->
                val data = if (datBr.name.endsWith(".br")) BrotliInputStream(BufferedInputStream(s, 1 shl 20)) else s
                TransferList.build(list, data, raw)
            }
            FileChannel.open(raw.toPath(), StandardOpenOption.READ).use { c -> importImage(ChannelSource(c), "system") }
            raw.delete()
            // a Treble OTA (8.0+) carries the vendor partition the same way; the firmware's apps and overlays live there.
            // With dynamic partitions (10+) product, system_ext and odm come as their own data files too.
            for (p in listOf("vendor", "product", "system_ext", "odm")) {
                val pDat = entries.firstOrNull { it.name == datBr.name.replace("system.new.dat", "$p.new.dat") } ?: continue
                val pList = entries.firstOrNull { it.name == listName.replace("system.transfer.list", "$p.transfer.list") } ?: continue
                runCatching {
                    onProgress("Building $p.img from OTA", 0.8f)
                    zip.getInputStream(pDat).use { s ->
                        val data = if (pDat.name.endsWith(".br")) BrotliInputStream(BufferedInputStream(s, 1 shl 20)) else s
                        TransferList.build(zip.getInputStream(pList).use { String(it.readBytes()) }, data, raw)
                    }
                    FileChannel.open(raw.toPath(), StandardOpenOption.READ).use { c ->
                        importImage(ChannelSource(c), if (p == "vendor" || p == "odm") p else "system/$p")
                    }
                }.onFailure { log("$p partition: ${it.message}") }
                raw.delete()
            }
        }
        if (sparseChunks.isNotEmpty() && !gotSystem) {
            val parts = sparseChunks.map { spillEntry(zip, it) }
            val chans = parts.map { FileChannel.open(it.toPath(), StandardOpenOption.READ) }
            try { importImage(SparseSource(chans.map { ChannelSource(it) }), "system") } finally { chans.forEach { it.close() }; parts.forEach { it.delete() } }
        }
        if (!names.any { it.startsWith("system/") } && !gotSystem) log("no system found in archive $name")
    }

    /** 7z: entries are read in order (solid archives cannot seek); interesting ones are unpacked like their zip twins */
    private fun importSevenZ(ch: FileChannel, name: String, depth: Int) {
        // the bundled 7-Zip decodes LZMA natively, ~10x the Java decoder (MiFlash Redmi 3: 31 s against 5+ min)
        val input = if (depth == 0) topInput else null
        if (input != null && !File(ctx.filesDir, "7z.off").exists()) {
            val dir = runCatching { extractWith7z(input, name) }.onFailure { log("7-Zip: ${it.message}; built-in reader") }.getOrNull()
            if (dir != null) {
                try { importExtracted(dir, name, depth) } finally { dir.deleteRecursively() }
                return
            }
        }
        importSevenZJava(ch, name, depth)
    }

    /** the whole archive into a temporary directory, minus partitions an emulator never uses */
    private fun extractWith7z(input: String, name: String): File {
        val bin = File(ctx.applicationInfo.nativeLibraryDir, "libsevenzip.so")
        if (!bin.canExecute()) throw IOException("not bundled")
        val out = File(tmp, "7z-${System.nanoTime()}").apply { deleteRecursively(); mkdirs() }
        val skip = listOf("userdata*.img", "cache.img", "NON-HLOS.bin", "modem*", "MODEM*", "CP_*", "*.mbn", "persist.img")
        val cmd = runner(bin) + listOf("x", "-y", "-bsp1", "-bb0", "-mmt=on", "-o${out.absolutePath}") + skip.map { "-xr!$it" } + input
        onProgress("Unpacking $name (7-Zip)", -1f)
        val t0 = System.currentTimeMillis()
        val code = runTool(cmd) { l -> Regex("""(\d+)%""").findAll(l).lastOrNull()?.let { onProgress("Unpacking $name (7-Zip)", it.groupValues[1].toInt() / 100f * 0.6f) } }
        if (code != 0) { out.deleteRecursively(); throw IOException("exit $code") }
        log("7-Zip: unpacked in ${(System.currentTimeMillis() - t0) / 1000} s")
        return out
    }

    /** the same choices as for the entries of a 7z read by the Java decoder, on files 7-Zip has written out */
    private fun importExtracted(dir: File, name: String, depth: Int) {
        val nested = ArrayList<Pair<File, String>>()
        dir.walkTopDown().filter { it.isFile }.forEach { f ->
            if (cancelled) throw ImportCancelledException()
            val n = f.relativeTo(dir).invariantSeparatorsPath
            val base = f.name
            when {
                n.contains("__MACOSX/") || base.startsWith("._") -> {}
                n.startsWith("system/") -> { f.inputStream().use { writeFile(n, it, null) }; gotSystem = true }
                base.equals("boot.img", true) -> takeBoot(f.readBytes())
                base.equals("recovery.img", true) && f.length() < 64_000_000 -> recovery = f.readBytes()
                base.matches(SYSTEM_IMAGE) -> nested.add(f to "system")
                base.matches(Regex("""(?i)(system|kernel|boot)\.sin""")) -> f.inputStream().use { importSin(it, base) }
                base.matches(Regex("""(?i).*\.(zip|tar|tar\.md5|md5|tgz|tar\.gz)""")) && f.length() > 20_000_000 &&
                    !base.startsWith("MODEM") && !base.startsWith("CP_") && !base.contains("CSC") -> nested.add(f to "archive")
            }
        }
        for ((f, kind) in nested) FileChannel.open(f.toPath(), StandardOpenOption.READ).use { c ->
            val src = ChannelSource(c)
            if (kind == "system") importImage(src, "system") else handle(src, c, f.name, depth + 1)
        }
        if (!gotSystem && nested.isEmpty()) log("no system found in archive $name")
    }

    /** a bundled static Linux tool, under pdgwrap (the sandbox's seccomp filter would kill it) */
    private fun runner(bin: File): List<String> {
        val wrap = File(ctx.applicationInfo.nativeLibraryDir, "libpdgwrap.so")
        return (if (wrap.canExecute()) listOf(wrap.absolutePath) else emptyList()) + bin.absolutePath
    }

    private fun runTool(cmd: List<String>, onLine: (String) -> Unit): Int {
        val pb = ProcessBuilder(cmd).redirectErrorStream(true)
        pb.environment()["GLIBC_TUNABLES"] = "glibc.pthread.rseq=0"
        val p = pb.start()
        val r = p.inputStream.bufferedReader()
        val buf = CharArray(4096)
        while (true) {
            val k = r.read(buf); if (k < 0) break
            if (cancelled) p.destroyForcibly()
            val chunk = String(buf, 0, k)
            chunk.lineSequence().filter { it.startsWith("pdgwrap:") }.forEach { log(it) }
            onLine(chunk)
        }
        val code = p.waitFor()
        if (cancelled) throw ImportCancelledException()
        return code
    }

    private fun importSevenZJava(ch: FileChannel, name: String, depth: Int) {
        // the format probe left the channel at byte 1100, and SevenZFile reads its signature from where it stands
        ch.position(0)
        SevenZFile.builder().setSeekableByteChannel(ch).get().use { z ->
            val nested = ArrayList<Pair<File, String>>()
            while (true) {
                if (cancelled) throw ImportCancelledException()
                val e = z.nextEntry ?: break
                if (e.isDirectory) continue
                val n = e.name.replace('\\', '/')
                val base = n.substringAfterLast('/')
                val stream = z.getInputStream(e)
                when {
                    n.contains("__MACOSX/") || base.startsWith("._") -> {}
                    n.startsWith("system/") -> { writeFile(n, stream, null); gotSystem = true }
                    base.equals("boot.img", true) -> takeBoot(stream.readBytes())
                    base.equals("recovery.img", true) && e.size < 64_000_000 -> recovery = stream.readBytes()
                    base.matches(SYSTEM_IMAGE) -> nested.add(spill(stream, base) to "system")
                    base.matches(Regex("(?i)(system|kernel|boot)\\.sin")) -> importSin(stream, base)
                    base.matches(Regex("(?i).*\\.(zip|tar|tar\\.md5|md5|tgz|tar\\.gz)")) && e.size > 20_000_000 &&
                        !base.startsWith("MODEM") && !base.startsWith("CP_") && !base.contains("CSC") ->
                        nested.add(spill(stream, base) to "archive")
                }
                onProgress("Extracting: $base", 0.5f)
            }
            for ((f, kind) in nested) {
                try {
                    FileChannel.open(f.toPath(), StandardOpenOption.READ).use { c ->
                        val src = ChannelSource(c)
                        if (kind == "system") importImage(src, "system")
                        else handle(src, c, f.name, depth + 1)
                    }
                } finally { f.delete() }
            }
            if (!gotSystem && nested.isEmpty()) log("no system found in archive $name")
        }
    }

    private fun withEntrySource(zip: ZipFile, e: ZipArchiveEntry, block: (RandomSource) -> Unit) {
        // несжатую запись читаем прямо из архива по смещению, сжатую — через временный файл
        val ch = zipChannel
        if (e.method == ZipArchiveEntry.STORED && ch != null) {
            runCatching { zip.getRawInputStream(e).close() } // вычисляет dataOffset
            if (e.dataOffset > 0) { block(ChannelSource(ch, e.dataOffset, e.size)); return }
        }
        val t = spillEntry(zip, e)
        deferred?.let { d -> d.add(t to e.name); return }
        FileChannel.open(t.toPath(), StandardOpenOption.READ).use { c -> block(ChannelSource(c)) }
        t.delete()
    }

    /** a nested zip: its images are copied out first, the zip deleted, then the images unpacked */
    private fun handleNestedZip(t: File, base: String, depth: Int) {
        val outer = deferred
        val mine = ArrayList<Pair<File, String>>()
        deferred = mine
        try {
            FileChannel.open(t.toPath(), StandardOpenOption.READ).use { c -> handle(ChannelSource(c), c, base, depth + 1) }
        } finally { deferred = outer; t.delete() }
        try {
            for ((f, entry) in mine) {
                val part = if (entry.substringAfterLast('/').lowercase().startsWith("vendor")) "vendor" else "system"
                FileChannel.open(f.toPath(), StandardOpenOption.READ).use { c ->
                    val src = ChannelSource(c)
                    val img = if (SparseSource.probe(src)) SparseSource(listOf(src)) else src
                    if (part == "vendor") runCatching { importImage(img, part) } else importImage(img, part)
                }
                f.delete()
            }
        } finally { mine.forEach { it.first.delete() } }
    }

    private fun spillEntry(zip: ZipFile, e: ZipArchiveEntry): File = zip.getInputStream(e).use { spill(it, e.name.substringAfterLast('/')) }

    private fun spill(i: InputStream, name: String): File {
        val t = File(tmp, "${System.nanoTime()}-$name")
        t.outputStream().use { o -> i.copyTo(o, 1 shl 20) }
        return t
    }

    /** [under]: the tar itself as a random-access source, when [s] reads it from its first byte (a tar that is the
     *  file being imported, not one inside a compressed stream or another archive) */
    private fun importTarStream(s: InputStream, name: String, depth: Int, under: RandomSource? = null) {
        val tar = TarArchiveInputStream(s, "UTF-8")
        var fullRoot: Boolean? = null
        while (true) {
            if (cancelled) throw ImportCancelledException()
            val e: TarArchiveEntry = tar.nextEntry ?: break
            var n = e.name.removePrefix("./").trimStart('/')
            if (n.isEmpty()) continue
            // Odin packages of 2011-2012 (GT-P7310 UELPB) hold each partition as "<name>.img.md5": the image with an
            // md5 line appended (Odin checks it); the file systems and the boot header give their own sizes, the tail is ignored
            val base = n.substringAfterLast('/').let { if (it.count { c -> c == '.' } >= 2) it.replace(Regex("(?i)\\.md5$"), "") else it }
            // дерево rootfs целиком (system/, data/, dev/, …)
            // Odin tars of 2009-2010 Samsungs (GT-I7500) carry raw partition dumps named without an extension
            // ("system", "kernel", "recovery" as regular files); in a rootfs tree "system" is a directory
            val bareImage = e.isFile && e.size > 1_000_000 && base.matches(Regex("(?i)system|factoryfs"))
            if (fullRoot == null && !bareImage && (n == "system" || n.startsWith("system/") || n.startsWith("init.rc") || n.startsWith("default.prop"))) fullRoot = true
            when {
                (bareImage || base.matches(SYSTEM_IMAGE) || base.matches(Regex("(?i)(system\\.img(\\.ext4)?|factoryfs\\.(img|rfs)|super\\.img)\\.lz4"))) && e.isFile -> {
                    if (under != null && !base.endsWith(".lz4", true)) {
                        // an Odin tar holds the image as it is: read in place, the phone needs no second copy of it
                        importImage(ExtentSource(under, listOf(tar.bytesRead to e.size)), "system")
                    } else {
                        val t = spill(maybeLz4(tar, base), base)
                        FileChannel.open(t.toPath(), StandardOpenOption.READ).use { c -> importImage(ChannelSource(c), "system") }
                        t.delete()
                    }
                }
                // other partitions as separate images (Samsung CSC: prism, optics; pre-super AP: vendor, product, odm)
                base.matches(PART_IMAGE) && e.isFile && e.size > 4096 -> {
                    val p = base.lowercase().substringBefore('.')
                    val mount = when (p) { "product", "system_ext" -> "system/$p"; "hidden" -> "preload"; else -> p }
                    runCatching {
                        val t = spill(maybeLz4(tar, base), base)
                        try { FileChannel.open(t.toPath(), StandardOpenOption.READ).use { c -> importImage(ChannelSource(c), mount) } } finally { t.delete() }
                    }.onFailure { log("$base skipped: ${it.message}") }
                }
                base.matches(Regex("(?i)(system|kernel|boot)\\.sin")) && e.isFile -> importSin(tar.nonClosing(), base)
                base.matches(Regex("(?i)(boot\\.img|zImage|kernel)(\\.lz4)?")) && e.isFile -> takeBoot(maybeLz4(tar, base).readBytes())
                base.matches(Regex("(?i)recovery\\.img(\\.lz4)?")) && e.isFile && e.size < 64_000_000 -> recovery = maybeLz4(tar, base).readBytes()
                base.matches(Regex("(?i).*\\.(tar|tar\\.md5)")) && e.isFile && e.size > 20_000_000 -> importTarStream(tar.nonClosing(), base, depth + 1)
                // factory-образы Google: tgz → image-*.zip → system.img/boot.img; Samsung: zip внутри tar
                base.lowercase().endsWith(".zip") && e.isFile && e.size > 20_000_000 -> {
                    handleNestedZip(spill(tar.nonClosing(), base), base, depth)
                }
                // (not in a TWRP system backup: its paths are relative to /system, and etc/, vendor/ there are
                // system/etc, system/vendor - taken by the branch below)
                !(name.contains("system", true) && name.endsWith(".win", true)) &&
                (n.matches(Regex("^(system|data|dev|sbin|vendor|etc)(/.*)?$")) || n.matches(Regex("^[^/]+\\.rc$")) || n == "default.prop" || n.startsWith("dhd.")) -> {
                    // файлы корня (рамдиск, dhd.*) берём, только если архив — целое дерево rootfs
                    if (fullRoot != true && !n.startsWith("system") && !(n.startsWith("dhd.") || n.endsWith(".rc") || n == "default.prop" || n.startsWith("sbin"))) continue
                    when {
                        e.isDirectory -> File(root, n).mkdirs()
                        e.isSymbolicLink -> symlinks.add(e.linkName to "/$n")
                        e.isLink -> hardlink(n, e.linkName)
                        e.isFile -> {
                            writeFile(n, tar.nonClosing(), e.mode)
                            if (n.startsWith("system/")) gotSystem = true
                        }
                    }
                }
                // TWRP system.ext4.win: пути без префикса system/
                name.contains("system", true) && name.endsWith(".win", true) -> {
                    val p = "system/$n"
                    when {
                        e.isDirectory -> File(root, p).mkdirs()
                        e.isSymbolicLink -> symlinks.add(e.linkName to "/$p")
                        e.isFile -> { writeFile(p, tar.nonClosing(), e.mode); gotSystem = true }
                    }
                }
            }
            if (files % 200 == 0) onProgress("Extracting: $base", -1f)
        }
    }

    private fun InputStream.nonClosing(): InputStream = object : java.io.FilterInputStream(this) { override fun close() {} }

    private fun maybeLz4(s: InputStream, name: String): InputStream =
        if (name.endsWith(".lz4", true)) org.apache.commons.compress.compressors.lz4.FramedLZ4CompressorInputStream(s.nonClosing()) else s.nonClosing()

    private fun hardlink(n: String, target: String) {
        val src = File(root, target.removePrefix("./").trimStart('/'))
        val dst = File(root, n)
        if (src.isFile) { dst.parentFile?.mkdirs(); src.copyTo(dst, overwrite = true) }
    }

    // ------------------------------------------------------------------ образы ФС

    /** A partition image of any supported file system: sparse → ext4, super.img (dynamic partitions), YAFFS2, FAT/RFS. */
    private fun importImage(src0: RandomSource, mount: String) {
        val src = if (SparseSource.probe(src0)) SparseSource(listOf(src0)) else src0
        when {
            Ext4Reader.probe(src) -> importExt4(src, mount)
            SuperImage.probe(src) -> {
                val parts = SuperImage.partitions(src)
                log("super.img: ${parts.keys.joinToString()}")
                fun pick(n: String) = parts[n]?.takeIf { it.isNotEmpty() } ?: parts["${n}_a"]?.takeIf { it.isNotEmpty() }
                importImage(ExtentSource(src, pick("system") ?: throw IOException("super.img without a system partition")), "system")
                for (p in listOf("vendor", "product", "system_ext", "odm")) pick(p)?.let { e ->
                    runCatching { importImage(ExtentSource(src, e), if (p == "vendor" || p == "odm") p else "system/$p") }
                        .onFailure { log("super.img: $p skipped: ${it.message}") }
                }
                for (p in parts.keys.map { it.removeSuffix("_a") }.filter { it.matches(SKIN_PARTITION) }.distinct()) pick(p)?.let { e ->
                    runCatching { importImage(ExtentSource(src, e), p) }.onFailure { log("super.img: $p skipped: ${it.message}") }
                }
            }
            Yaffs2Image.probe(src) -> importTree(Yaffs2Image(src), mount, "YAFFS2")
            FatImage.probe(src) -> importTree(FatImage(src), mount, "FAT/RFS")
            ErofsReader.probe(src) -> importTree(ErofsImage(src), mount, "EROFS")
            else -> throw IOException("unknown file system in the $mount image")
        }
    }

    private fun importTree(tree: TreeSource, mount: String, kind: String) {
        onProgress("Reading image $mount ($kind)", -1f)
        var n = 0
        tree.walk { path, isDir, link, mode, size, data ->
            if (cancelled) throw ImportCancelledException()
            val rel = "$mount/$path"
            val f = File(root, rel)
            if (!f.canonicalPath.startsWith(root.canonicalPath)) return@walk
            when {
                isDir -> f.mkdirs()
                link != null -> symlinks.add(link to "/$rel")
                data != null -> {
                    f.parentFile?.mkdirs()
                    data().use { i -> f.outputStream().buffered(1 shl 20).use { o -> i.copyTo(o, 1 shl 16) } }
                    applyMode(f, mode and 0xfff)
                    files++; bytes += size
                }
            }
            if (++n % 150 == 0) onProgress("$mount: ${path.substringAfterLast('/')}", -1f)
        }
        if (mount == "system") { gotSystem = true; hoistSystemAsRoot() }
        log("image $mount ($kind): $n objects")
    }

    /** Sony SIN v3: unpacked into a sparse temporary image, then read like any partition image. */
    private fun importSin(s: InputStream, base: String) {
        val raw = File(tmp, "${System.nanoTime()}-$base.img")
        try {
            onProgress("Unpacking $base", -1f)
            // the system partition is read out of the SIN itself (a copy of the SIN takes far less room than the
            // unpacked partition); the layouts that cannot be read in place are unpacked as before
            if (base.startsWith("system", true)) {
                s.use { i -> raw.outputStream().use { o -> i.copyTo(o, 1 shl 20) } }
                if (cancelled) throw ImportCancelledException()
                val inPlace = FileChannel.open(raw.toPath(), StandardOpenOption.READ).use { c ->
                    SinImage.source(c)?.also { importImage(it, "system") } != null
                }
                if (inPlace) return
                val sin = File(tmp, "${System.nanoTime()}-$base")
                if (!raw.renameTo(sin)) throw IOException("cannot move $raw")
                try { sin.inputStream().use { SinImage.unpack(it, raw) { if (cancelled) throw ImportCancelledException() } } } finally { sin.delete() }
            } else
            SinImage.unpack(s, raw) { if (cancelled) throw ImportCancelledException() }
            if (base.startsWith("kernel", true) || base.startsWith("boot", true)) { takeBoot(raw.readBytes()); return }
            if (base.startsWith("recovery", true) || base.startsWith("fotakernel", true)) return
            FileChannel.open(raw.toPath(), StandardOpenOption.READ).use { c -> importImage(ChannelSource(c), base.substringBefore('.').lowercase()) }
        } finally { raw.delete() }
    }

    /** Spreadtrum .pac: partition files are slices of the package; system, boot and recovery are taken from it. */
    /**
     * Motorola .sbf (RSD Lite "Multi-Interface Super File"). From 0x300 a table of 32-byte code group entries: byte 3
     * the group number, the load address at 6 (low u16, LE) and 8 (high u16), the last byte's address at 10/12.
     * The data follow as records 03 02 02 00 00 00 00 03 02 00 00 00 00 00 03 02 01, size (BE32), load address
     * (BE32), the bytes, a 4-byte checksum. (Checked on the Droid FRG83G file: CG39 0xd0ef4000..0xd8f577bf is the
     * 0x080637c0-byte record of YAFFS2 pages 2048+64, CG35 0xc0680000 an Android boot image.) Groups by Motorola's
     * numbering: 35 boot, 39 system. Tegra SBFs number them otherwise (Xoom MZ604 HTJ85B: CG57 boot, CG58 recovery,
     * CG59 a 256 MB ext4 system), so a record is told by its content: Android boot images ("ANDROID!") are boot, then
     * recovery, in file order; the system is CG39 or else the largest other record.
     */
    private fun importSbf(src: RandomSource, ch: FileChannel) {
        val tab = ByteBuffer.allocate(0x500).order(ByteOrder.LITTLE_ENDIAN)
        src.read(0x300, tab); tab.flip()
        val start = HashMap<Long, Int>()
        for (o in 0 until 0x500 - 32 step 32) {
            val cg = tab.get(o + 3).toInt() and 0xff
            if ((0 until 32).all { tab.get(o + it).toInt() == 0 }) break   // the table ends with an empty entry
            val a = ((tab.getShort(o + 8).toLong() and 0xffff) shl 16) or (tab.getShort(o + 6).toLong() and 0xffff)
            start[a] = cg
        }
        log("sbf: code groups ${start.values.sorted()}")
        val pre = byteArrayOf(3, 2, 2, 0, 0, 0, 0, 3, 2, 0, 0, 0, 0, 0, 3, 2, 1)
        val win = ByteBuffer.allocate(1 shl 20)
        var pos = 0L
        var boot: ByteArray? = null
        var sys: Pair<Long, Long>? = null
        var big: Pair<Long, Long>? = null
        while (pos < src.size) {
            if (cancelled) throw ImportCancelledException()
            win.clear(); src.read(pos, win); win.flip()
            val b = win.array(); val n = minOf(win.limit().toLong(), src.size - pos).toInt()
            var hit = -1
            var i = 0
            while (i + pre.size + 8 <= n) {
                var k = 0
                while (k < pre.size && b[i + k] == pre[k]) k++
                if (k == pre.size) { hit = i; break }
                i++
            }
            if (hit < 0) { pos += maxOf(1, n - pre.size - 8); continue }
            val hb = ByteBuffer.wrap(b, hit + pre.size, 8).order(ByteOrder.BIG_ENDIAN)
            val size = hb.int.toLong() and 0xffffffffL; val addr = hb.int.toLong() and 0xffffffffL
            val data = pos + hit + pre.size + 8
            if (data + size > src.size) { pos += hit + 1; continue }
            val magic = ByteBuffer.allocate(8).also { src.read(data, it); it.flip() }
            val isBootImg = size in 8..63_999_999 && String(magic.array(), Charsets.ISO_8859_1) == "ANDROID!"
            when {
                isBootImg -> {
                    val img = ByteArray(size.toInt()).also { src.read(data, ByteBuffer.wrap(it)) }
                    // Xoom puts recovery first: the one whose init starts app_process (zygote) is boot
                    when (startsZygote(img)) {
                        true -> boot = img
                        false -> if (recovery == null) recovery = img
                        null -> if (boot == null) boot = img else if (recovery == null) recovery = img
                    }
                }
                start[addr] == 39 -> sys = data to size
                big == null || size > big!!.second -> big = data to size
            }
            onProgress("Reading SBF", 0.3f * (data + size) / src.size)
            pos = data + size
        }
        boot?.let { takeBoot(it) }
        val (off, size) = sys ?: big?.takeIf { it.second > 32_000_000 } ?: throw IOException("sbf: no system code group")
        importImage(ChannelSource(ch, off, size), "system")
    }

    /** An Android boot image (bootimg.h v0: page size at 36, kernel/ramdisk sizes at 8/16) whose gzip ramdisk names
     *  /system/bin/app_process (init.rc's zygote) - a boot, not a recovery; null when the ramdisk cannot be read. */
    private fun startsZygote(img: ByteArray): Boolean? = runCatching {
        val b = ByteBuffer.wrap(img).order(ByteOrder.LITTLE_ENDIAN)
        val ks = b.getInt(8).toLong() and 0xffffffffL; val rs = b.getInt(16); val ps = b.getInt(36).takeIf { it in 2048..16384 } ?: 2048
        val ro = (ps + (ks + ps - 1) / ps * ps).toInt()
        if (rs <= 0 || ro + rs > img.size || img[ro] != 0x1f.toByte() || img[ro + 1] != 0x8b.toByte()) return@runCatching null
        val rd = GZIPInputStream(java.io.ByteArrayInputStream(img, ro, rs)).use { it.readBytes() }
        String(rd, Charsets.ISO_8859_1).contains("/system/bin/app_process")
    }.getOrNull()

    private fun importPac(src: RandomSource, ch: FileChannel) {
        val list = PacFile.entries(src)
        log("PAC: ${list.joinToString { "${it.id}=${it.name}" }}")
        fun slice(e: PacFile.Entry) = ChannelSource(ch, e.offset, e.size)
        fun isPart(e: PacFile.Entry, p: String) = e.id.equals(p, true) || e.name.substringBefore('.').equals(p, true)
        list.firstOrNull { isPart(it, "boot") }?.let { e ->
            val b = ByteBuffer.allocate(e.size.toInt()); slice(e).read(0, b); takeBoot(b.array())
        }
        list.firstOrNull { isPart(it, "recovery") }?.takeIf { it.size < 64_000_000 }?.let { e ->
            val b = ByteBuffer.allocate(e.size.toInt()); slice(e).read(0, b); recovery = b.array()
        }
        val sys = list.firstOrNull { isPart(it, "system") } ?: list.firstOrNull { isPart(it, "super") }
            ?: throw IOException("PAC without a system image")
        importImage(slice(sys), "system")
        list.firstOrNull { isPart(it, "vendor") }?.let { e -> runCatching { importImage(slice(e), "vendor") } }
    }

    /** A full A/B OTA: every partition with files of the firmware is taken out of payload.bin in turn and read. */
    private fun importPayload(src: RandomSource, input: String? = null) {
        log("OTA payload.bin")
        val names = PayloadReader.partitionNames(src).map { it.removeSuffix("_a") }.toSet()
        val extra = names.filter { it.matches(SKIN_PARTITION) }.distinct()
        val wanted = (listOf("system" to "system", "vendor" to "vendor", "odm" to "odm",
            "product" to "system/product", "system_ext" to "system/system_ext") + extra.map { it to it }).filter { it.first in names }
        if ("system" !in names) throw IOException("payload.bin without a system partition")
        // payload-dumper-go decompresses every operation in parallel: several times faster than the reader below
        // (files/pdg.off: the built-in reader only, to compare)
        val dumped = input?.takeIf { !File(ctx.filesDir, "pdg.off").exists() }?.let { runCatching { dumpWithGo(it, wanted.map { w -> w.first }) }.onFailure { e -> log("payload-dumper-go: ${e.message}; built-in reader") }.getOrNull() }
        for ((part, mount) in wanted) {
            val img = dumped?.let { File(it, "$part.img") }?.takeIf { it.isFile } ?: File(tmp, "${System.nanoTime()}-$part.img").also { f ->
                val t0 = System.currentTimeMillis()
                val ok = PayloadReader.extractPartition(src, part, f) { p -> onProgress("Extracting $part from payload.bin", p) }
                log("payload.bin: $part by the built-in reader in ${(System.currentTimeMillis() - t0) / 1000} s")
                if (!ok) { f.delete(); if (part == "system") throw IOException("payload.bin without a system partition") }
            }
            if (!img.isFile) continue
            try {
                FileChannel.open(img.toPath(), StandardOpenOption.READ).use { c ->
                    if (part == "system") importImage(ChannelSource(c), mount)
                    else runCatching { importImage(ChannelSource(c), mount) }.onFailure { log("payload.bin: $part skipped: ${it.message}") }
                }
            } finally {
                img.delete()
            }
        }
        dumped?.deleteRecursively()
    }

    /** runs the bundled payload-dumper-go (libpayloaddumper.so) on [input]; the directory with <part>.img files */
    private fun dumpWithGo(input: String, parts: List<String>): File {
        val bin = File(ctx.applicationInfo.nativeLibraryDir, "libpayloaddumper.so")
        if (!bin.canExecute()) throw IOException("not bundled")
        val out = File(tmp, "pdg-${System.nanoTime()}").apply { deleteRecursively() }
        // run under pdgwrap: the sandbox's seccomp filter kills a static glibc/Go binary on calls bionic never makes;
        // the wrapper turns those into ENOSYS
        val wrap = File(ctx.applicationInfo.nativeLibraryDir, "libpdgwrap.so")
        val cmd = (if (wrap.canExecute()) listOf(wrap.absolutePath) else emptyList()) + listOf(bin.absolutePath, "-o", out.absolutePath, "-p", parts.joinToString(","),
            "-c", Runtime.getRuntime().availableProcessors().toString(), "-no-verify", "-no-fec", input)
        onProgress("Extracting ${parts.joinToString(", ")} (payload-dumper-go)", -1f)
        val t0 = System.currentTimeMillis()
        val pb = ProcessBuilder(cmd).redirectErrorStream(true)
        // a static glibc binary: glibc registers rseq at start, which the app sandbox's seccomp filter kills (SIGSYS, exit 159)
        pb.environment()["GLIBC_TUNABLES"] = "glibc.pthread.rseq=0"
        val p = pb.start()
        val tail = ArrayDeque<String>()
        p.inputStream.bufferedReader().forEachLine { l ->
            if (cancelled) p.destroyForcibly()
                if (l.startsWith("pdgwrap:")) log(l)
            tail.addLast(l.take(200)); if (tail.size > 6) tail.removeFirst()
        }
        val code = p.waitFor()
        if (cancelled) { out.deleteRecursively(); throw ImportCancelledException() }
        if (code != 0 || !File(out, "system.img").isFile) {
            out.deleteRecursively()
            throw IOException("exit $code: ${tail.joinToString(" | ")}")
        }
        log("payload-dumper-go: ${parts.size} partitions in ${(System.currentTimeMillis() - t0) / 1000} s")
        return out
    }

    private fun importExt4(src: RandomSource, mount: String) {
        val fs = Ext4Reader(src)
        onProgress("Reading image $mount (ext4, block ${fs.blockSize})", -1f)
        var n = 0
        fs.walk { path, node ->
            if (cancelled) throw ImportCancelledException()
            val rel = "$mount/$path"
            val f = File(root, rel)
            when {
                node.isDir -> f.mkdirs()
                node.isLink -> symlinks.add(fs.linkTarget(node) to "/$rel")
                node.isFile -> {
                    f.parentFile?.mkdirs()
                    f.outputStream().buffered(1 shl 20).use { fs.copy(node, it) }
                    applyMode(f, node.perm)
                    files++; bytes += node.size
                }
            }
            if (++n % 150 == 0) onProgress("$mount: ${path.substringAfterLast('/')}", -1f)
        }
        if (mount == "system") { gotSystem = true; hoistSystemAsRoot() }
        log("image $mount: $n objects")
    }

    /**
     * A "system-as-root" image (every A/B device, and all of them from Android 10) holds the whole root directory, with
     * the system partition's files in its /system. It was read under <root>/system like a plain system image, so it is
     * moved one level up: <root>/system/system becomes <root>/system, and the links it brought (/bin, /etc, /product,
     * /system_ext, /init...) land in the root where they belong.
     */
    private fun hoistSystemAsRoot() {
        val outer = File(root, "system")
        val inner = File(outer, "system")
        if (!File(inner, "framework").isDirectory && !File(inner, "build.prop").isFile) return
        val aside = File(root, ".system-as-root")
        if (!outer.renameTo(aside)) { log("system-as-root: cannot move the tree"); return }
        aside.listFiles()?.forEach { f ->
            val dst = File(root, f.name)
            if (dst.exists() && dst.isDirectory && f.isDirectory) { f.listFiles()?.forEach { c -> c.renameTo(File(dst, c.name)) }; f.delete() }
            else f.renameTo(dst)
        }
        aside.delete()
        for (i in symlinks.indices) {
            val (target, link) = symlinks[i]
            if (link.startsWith("/system/")) symlinks[i] = target to link.removePrefix("/system")
        }
        log("system-as-root image: the tree is taken as the root directory")
    }

    private fun takeBoot(data: ByteArray) {
        if (ramdisk != null) return
        val rd = runCatching { BootImage.ramdisk(data) }.getOrNull()
        if (rd.isNullOrEmpty()) { log("boot: ramdisk not recognized"); return }
        ramdisk = rd
        log("boot: ramdisk, ${rd.size} files")
    }

    // ------------------------------------------------------------------ файлы, ссылки, права

    private fun writeFile(rel: String, i: InputStream, mode: Int?) {
        val f = File(root, rel)
        if (!f.canonicalPath.startsWith(root.canonicalPath)) return // защита от ../ в архиве
        f.parentFile?.mkdirs()
        if (runCatching { android.system.OsConstants.S_ISLNK(Os.lstat(f.path).st_mode) }.getOrDefault(false)) f.delete()
        f.outputStream().buffered(1 shl 20).use { o -> bytes += i.copyTo(o, 1 shl 16) }
        if (mode != null) applyMode(f, mode and 0xfff)
        files++
    }

    private fun applyMode(f: File, perm: Int) {
        if (perm and 0x49 != 0) f.setExecutable(true, false)
        f.setReadable(true, false)
    }

    private fun parseUpdaterScript(s: String) {
        val text = s.replace(Regex("#[^\n]*"), "")
        for (m in Regex("symlink\\s*\\(([^;]*?)\\)\\s*;", RegexOption.DOT_MATCHES_ALL).findAll(text)) {
            val args = Regex("\"([^\"]*)\"").findAll(m.groupValues[1]).map { it.groupValues[1] }.toList()
            if (args.size >= 2) for (link in args.drop(1)) symlinks.add(args[0] to link)
        }
        for (m in Regex("set_perm_recursive\\s*\\(([^;]*?)\\)\\s*;", RegexOption.DOT_MATCHES_ALL).findAll(text)) {
            val a = m.groupValues[1].split(',').map { it.trim().trim('"') }
            if (a.size >= 5) a[3].toIntOrNull(8)?.let { perms.add(Triple(a[4], it, true)) }
        }
        for (m in Regex("set_perm\\s*\\(([^;]*?)\\)\\s*;", RegexOption.DOT_MATCHES_ALL).findAll(text)) {
            val a = m.groupValues[1].split(',').map { it.trim().trim('"') }
            if (a.size >= 4) a[2].toIntOrNull(8)?.let { perms.add(Triple(a[3], it, false)) }
        }
        for (m in Regex("set_metadata(_recursive)?\\s*\\(([^;]*?)\\)\\s*;", RegexOption.DOT_MATCHES_ALL).findAll(text)) {
            val a = m.groupValues[2].split(',').map { it.trim().trim('"') }
            if (a.isEmpty()) continue
            val key = if (m.groupValues[1].isNotEmpty()) "fmode" else "mode"
            val i = a.indexOf(key)
            if (i > 0 && i + 1 < a.size) a[i + 1].toIntOrNull(8)?.let { perms.add(Triple(a[0], it, m.groupValues[1].isNotEmpty())) }
        }
        log("updater-script: ${symlinks.size} symlinks, ${perms.size} perms")
    }

    /** Ссылки (с переводом абсолютных целей в относительные), права, служебные ссылки корня. */
    private fun finishTree() {
        // рамдиск: init*.rc, default.prop, sbin/, file_contexts…
        ramdisk?.forEach { e ->
            val type = e.mode and 0xF000
            val n = e.name
            if (n.isEmpty() || n == "." || n.startsWith("system/") || n.startsWith("data/") || n.startsWith("dev/") || n.startsWith("proc") || n.startsWith("sys/")) return@forEach
            val f = File(root, n)
            when (type) {
                0x4000 -> f.mkdirs()
                0xA000 -> symlinks.add(String(e.data) to "/$n")
                0x8000 -> {
                    if (n == "init" || n.startsWith("sbin/ueventd") || n.startsWith("sbin/adbd") || n == "charger") return@forEach
                    f.parentFile?.mkdirs(); f.writeBytes(e.data)
                    applyMode(f, e.mode and 0xfff)
                }
            }
        }
        var made = 0
        for ((target, link) in symlinks) {
            val rel = link.trimStart('/')
            val f = File(root, rel)
            if (!f.canonicalPath.startsWith(root.canonicalPath)) continue
            f.parentFile?.mkdirs()
            val t = relTarget(link, target)
            runCatching {
                if (f.exists() || isLink(f)) { if (f.isDirectory && !isLink(f)) return@runCatching; f.delete() }
                Os.symlink(t, f.absolutePath); made++
            }
        }
        for ((p, mode, rec) in perms) {
            val f = File(root, p.trimStart('/'))
            if (rec) f.walkTopDown().filter { it.isFile }.forEach { applyMode(it, mode) } else if (f.isFile) applyMode(f, mode)
        }
        // исполняемые — всё в bin/xbin/sbin
        for (d in listOf("system/bin", "system/xbin", "sbin", "vendor/bin", "system/vendor/bin")) {
            File(root, d).listFiles()?.forEach { if (it.isFile) it.setExecutable(true, false) }
        }
        standardLinks()
        fun rootLink(name: String, target: String) {
            val f = File(root, name)
            if (!f.exists() && !isLink(f) && File(root, target).exists()) runCatching { Os.symlink(target, f.absolutePath) }
        }
        rootLink("vendor", "system/vendor")
        rootLink("etc", "system/etc")
        rootLink("bin", "system/bin")
        runCatching { File(root, "system/etc/mtab").let { if (!it.exists() && !isLink(it)) Os.symlink("../../proc/mounts", it.absolutePath) } }
        log("symlinks created: $made")
    }

    private fun isLink(f: File) = runCatching { android.system.OsConstants.S_ISLNK(Os.lstat(f.absolutePath).st_mode) }.getOrDefault(false)

    /** Абсолютную цель ссылки гостя делаем относительной — иначе ядро телефона уйдёт за пределы дерева. */
    /**
     * Архивы без updater-script и без ссылок в самом архиве (multirom, выгрузки system/ из TWRP) не
     * содержат sh → mksh и ссылок на апплеты toolbox — без них не стартует ни одна служба init.
     * Создаём их сами, если их нет: апплет берём, только если его имя есть в самом toolbox.
     */
    private fun standardLinks() {
        val bin = File(root, "system/bin")
        fun missing(n: String) = File(bin, n).let { !it.exists() && !isLink(it) }
        var made = 0
        if (missing("sh")) {
            val sh = listOf("mksh", "ash").firstOrNull { File(bin, it).isFile }
            if (sh != null && runCatching { Os.symlink(sh, File(bin, "sh").absolutePath) }.isSuccess) made++
        }
        val toolbox = File(bin, "toolbox")
        if (toolbox.isFile) {
            val text = runCatching { String(toolbox.readBytes(), Charsets.ISO_8859_1) }.getOrDefault("")
            for (a in TOOLBOX_APPLETS) {
                if (!missing(a)) continue
                if (!text.contains("\u0000$a\u0000")) continue
                if (runCatching { Os.symlink("toolbox", File(bin, a).absolutePath) }.isSuccess) made++
            }
        }
        if (made > 0) log("sh/toolbox symlinks created: $made (missing from archive)")
    }

    private fun relTarget(link: String, target: String): String {
        if (!target.startsWith("/")) return target
        val from = link.trimStart('/').split('/').dropLast(1)
        val to = target.trimStart('/').split('/').filter { it.isNotEmpty() }
        var i = 0
        while (i < from.size && i < to.size && from[i] == to[i]) i++
        val ups = List(from.size - i) { ".." }
        return (ups + to.drop(i)).joinToString("/").ifEmpty { "." }
    }

    companion object {
        /** partition images other than system inside a tar (taken after system, which SYSTEM_IMAGE catches first);
         *  hidden.img is Samsung's /preload (Galaxy S2 4.x mounts it there; /system/app links into it) */
        private val PART_IMAGE = Regex("(?i)(vendor|product|odm|prism|optics|hidden)\\.img(\\.ext4)?(\\.lz4)?")
        /** system partition images by name (ext4/sparse, Samsung RFS, YAFFS2 dumps, super.img of 10+, erofs, f2fs) */
        private val SYSTEM_IMAGE = Regex("(?i)system(_[a-z0-9]+)?(\\.ext4|\\.erofs|\\.f2fs)?\\.img(\\.ext4)?|" +
            "system_image\\.img|system\\.raw\\.img|factoryfs\\.(img|rfs)|" +
            "system\\.(rfs|yaffs2?|ext4|erofs|f2fs)|super(\\.raw)?\\.img|system_[ab]\\.img")
        /** partitions a vendor's skin adds beside the AOSP ones, mounted at the root under their own name: Xiaomi
         *  mi_ext (HyperOS apps and ro.mi.os.*), OPPO/realme/OnePlus my_*, vivo vgc */
        private val SKIN_PARTITION = Regex("mi_ext|my_[a-z]+|vgc|prism|optics")
        /** Апплеты toolbox Android 2.3–6.0 (ссылка создаётся, только если апплет есть в бинарнике). */
        private val TOOLBOX_APPLETS = listOf(
            "cat", "chcon", "chmod", "chown", "clear", "cmp", "cp", "date", "dd", "df", "dmesg", "du", "getenforce",
            "getevent", "getprop", "getsebool", "grep", "hd", "id", "ifconfig", "iftop", "insmod", "ioctl", "ionice",
            "kill", "ln", "load_policy", "log", "ls", "lsmod", "lsof", "md5", "mkdir", "mkswap", "mount", "mv",
            "nandread", "netstat", "newfs_msdos", "nohup", "notify", "printenv", "ps", "readlink", "renice",
            "restorecon", "rm", "rmdir", "rmmod", "route", "runcon", "schedtop", "sendevent", "setenforce",
            "setprop", "setsebool", "sleep", "smd", "start", "stop", "swapoff", "swapon", "sync", "top", "touch",
            "umount", "uptime", "vmstat", "watchprops", "wipe", "chroot", "sh",
        ).filter { it != "sh" }
    }
}
