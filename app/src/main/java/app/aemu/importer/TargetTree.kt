package app.aemu.importer

import java.io.ByteArrayInputStream
import java.io.File
import java.io.IOException
import java.nio.file.Files
import java.nio.file.LinkOption

/** Builds the import tree in an exclusively owned directory, without following image-supplied links. */
internal class TargetTree(private val root: File, private val cancelled: () -> Boolean) {
    companion object {
        internal fun applyFileMode(file: File, mode: Int) {
            if (mode and 0x49 != 0 && !file.setExecutable(true, false)) throw IOException("Cannot set executable mode: $file")
            if (!file.setReadable(true, false)) throw IOException("Cannot set readable mode: $file")
        }
    }
    private val writer = SafeTreeWriter(root, 2_000_000, 128L * 1024 * 1024 * 1024, cancelled)
    private val links = linkedMapOf<String, String>()
    private var systemDone = false
    private val seenPartitions = mutableSetOf<String>()

    fun partition(name: String, tree: TreeSource) {
        val mount = when (name) {
            "system" -> "system"
            "vendor", "odm", "system_dlkm", "vendor_dlkm" -> name
            "product", "system_ext" -> "system/$name"
            else -> throw IOException("Unexpected firmware partition: $name")
        }
        if (!seenPartitions.add(name)) throw IOException("Duplicate target partition: $name")
        if (name != "system" && !systemDone) throw IOException("System must be extracted first")
        if (name != "system") {
            // A separate partition replaces the system image's mountpoint, rather than merging its hidden contents.
            val mountPath = File(root, mount)
            if (Files.isSymbolicLink(mountPath.toPath())) throw IOException("Unexpected link at mountpoint")
            if (mountPath.exists() && !mountPath.deleteRecursively()) throw IOException("Cannot clear partition mountpoint: $mount")
            links.keys.removeAll { it == mount || it.startsWith("$mount/") }
        }
        tree.walk { path, isDir, link, mode, size, data ->
            if (cancelled()) throw ImportCancelledException()
            val rel = "$mount/$path"
            when {
                isDir -> writer.directory(rel)
                link != null -> {
                    if (links.size >= 2_000_000) throw IOException("Too many firmware links")
                    if (links.putIfAbsent(rel, link) != null) throw IOException("Duplicate link: $rel")
                }
                data != null -> { writer.write(rel, size, data()); applyFileMode(File(root, rel), mode) }
                else -> throw IOException("Unsupported filesystem entry: $rel")
            }
        }
        if (name == "system") { hoist(); systemDone = true }
    }

    private fun hoist() {
        val outer = File(root, "system")
        val inner = File(outer, "system")
        if (!File(inner, "framework").isDirectory && !File(inner, "build.prop").isFile) return
        val aside = File(root, ".system-as-root")
        Files.move(outer.toPath(), aside.toPath())
        val entries = aside.listFiles() ?: throw IOException("Cannot list system partition")
        for (f in entries) Files.move(f.toPath(), File(root, f.name).toPath())
        Files.delete(aside.toPath())
        val remapped = links.mapKeys { (path, _) -> path.removePrefix("system/") }
        if (remapped.size != links.size) throw IOException("Conflicting system links")
        links.clear(); links.putAll(remapped)
    }

    fun finish(ramdisk: List<BootImage.CpioEntry>) {
        if (!systemDone) throw IOException("Missing system partition")
        for (entry in ramdisk) {
            if (cancelled()) throw ImportCancelledException()
            val n = entry.name
            if (n.isEmpty() || n == "." || n == "system" || n.startsWith("system/") || n == "data" || n.startsWith("data/") || n == "dev" || n.startsWith("dev/") || n == "proc" || n.startsWith("proc/") || n == "sys" || n.startsWith("sys/")) continue
            when (entry.mode and 0xf000) {
                0x4000 -> writer.directory(n)
                0xa000 -> {
                    if (links.size >= 2_000_000) throw IOException("Too many firmware links")
                    if (links.putIfAbsent(n, String(entry.data, Charsets.UTF_8)) != null) throw IOException("Duplicate ramdisk link")
                }
                0x8000 -> {
                    if (n == "init" || n.startsWith("sbin/ueventd") || n.startsWith("sbin/adbd") || n == "charger") continue
                    writer.write(n, entry.data.size.toLong(), ByteArrayInputStream(entry.data))
                    applyFileMode(File(root, n), entry.mode)
                }
                else -> throw IOException("Unsupported ramdisk entry: $n")
            }
        }
        // These two links are added by the prior importer and are absent in the designated image.
        fun missing(name: String) = !links.containsKey(name) && !Files.exists(File(root, name).toPath(), LinkOption.NOFOLLOW_LINKS)
        if (File(root, "system/etc").isDirectory && missing("system/etc/mtab"))
            links["system/etc/mtab"] = "../../proc/mounts"
        val toolbox = File(root, "system/bin/toolbox")
        if (missing("system/bin/ioctl") && Files.isRegularFile(toolbox.toPath(), LinkOption.NOFOLLOW_LINKS) && toolboxContainsIoctl(toolbox))
            links["system/bin/ioctl"] = "toolbox"
        val conflicts = links.filterKeys { name -> Files.isDirectory(File(root, name).toPath(), LinkOption.NOFOLLOW_LINKS) }
        if (conflicts.isNotEmpty()) throw IOException("Links conflict with directories: " + conflicts.entries.take(32).joinToString { "${it.key} -> ${it.value}" })
        writer.links(links)
        // Preserve the existing importer behavior for guest binaries, including links to files.
        val base = root.toPath().toRealPath()
        for (name in listOf("system/bin", "system/xbin", "sbin", "vendor/bin", "system/vendor/bin")) {
            if (cancelled()) throw ImportCancelledException()
            val dir = File(root, name)
            if (!dir.exists()) continue
            if (!dir.isDirectory || !dir.toPath().toRealPath().startsWith(base)) throw IOException("Invalid guest binary directory: $name")
            val files = dir.listFiles() ?: throw IOException("Cannot list guest binary directory: $name")
            for (file in files) {
                if (cancelled()) throw ImportCancelledException()
                if (file.isFile) {
                    if (!file.toPath().toRealPath().startsWith(base)) throw IOException("Guest binary escapes root: $file")
                    if (!file.setExecutable(true, false)) throw IOException("Cannot set executable mode: $file")
                }
            }
        }
    }
    private fun toolboxContainsIoctl(file: File): Boolean {
        val needle = byteArrayOf(0, 'i'.code.toByte(), 'o'.code.toByte(), 'c'.code.toByte(), 't'.code.toByte(), 'l'.code.toByte(), 0)
        var matched = 0
        file.inputStream().buffered().use { input ->
            val buffer = ByteArray(65536)
            while (true) {
                if (cancelled()) throw ImportCancelledException()
                val n = input.read(buffer)
                if (n < 0) return false
                for (i in 0 until n) {
                    val b = buffer[i]
                    matched = if (b == needle[matched]) matched + 1 else if (b == needle[0]) 1 else 0
                    if (matched == needle.size) return true
                }
            }
        }
    }

}

