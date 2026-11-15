package app.aemu.core

import java.io.File
import java.nio.file.Files
import java.nio.file.LinkOption
import java.nio.file.Paths

/** Target-only execution of the vendor's config reader, after persistent properties load. */
internal object TargetVariantId {
    private const val SERVICE = "variantidBin"
    private const val BINARY = "/vendor/bin/variantid"
    private const val SIZE = 524288L

    private fun regularInside(root: File, relative: String): File? {
        var path = root.toPath()
        for (part in relative.split('/')) {
            path = path.resolve(part)
            if (Files.isSymbolicLink(path)) return null
        }
        return if (Files.isRegularFile(path, LinkOption.NOFOLLOW_LINKS)) path.toFile() else null
    }

    fun eligible(root: File, api: Int, model: String, abi: String, propertyReady: Boolean, linkerReady: Boolean): Boolean {
        if (api != 36 || model != "NX809J" || abi != "arm64-v8a" || !propertyReady || !linkerReady) return false
        return try {
            val rcFile = regularInside(root, "vendor/etc/init/zcustom.rc") ?: return false
            regularInside(root, "vendor/bin/variantid") ?: return false
            val cfg = regularInside(root, "dev/block/by-name/ztecfg") ?: return false
            var dir = root.toPath()
            for (part in listOf("dev", "block", "bootdevice", "by-name")) {
                dir = dir.resolve(part)
                if (Files.isSymbolicLink(dir) || !Files.isDirectory(dir, LinkOption.NOFOLLOW_LINKS)) return false
            }
            val alias = dir.resolve("ztecfg")
            if (!Files.isSymbolicLink(alias) || Files.readSymbolicLink(alias) != Paths.get("../../by-name/ztecfg")) return false
            if (cfg.length() != SIZE || !Files.isSameFile(cfg.toPath(), alias)) return false
            val service = InitPlan.parse(listOf(rcFile)).services[SERVICE] ?: return false
            if (service.argv != listOf(BINARY) || service.user != "root" || service.disabled || !service.oneshot || service.cls != "main") return false
            var trigger = false
            var start = false
            for (raw in rcFile.readLines()) {
                val words = raw.substringBefore('#').trim().split(Regex("\\s+"))
                when (words.firstOrNull()) {
                    "on" -> trigger = words == listOf("on", "load_persist_props_action")
                    "service", "import" -> trigger = false
                    "start" -> if (trigger && words == listOf("start", SERVICE)) start = true
                }
            }
            start
        } catch (_: Exception) { false }
    }

    /** Ignore process output because it may contain private device configuration. */
    fun run(command: (List<String>) -> BoundedGuestCommand.Result): String = when (val result = command(listOf(BINARY))) {
        is BoundedGuestCommand.Result.Completed -> "variantid: exit ${result.exitCode}; configuration and theme not verified"
        is BoundedGuestCommand.Result.Uncertain -> throw IllegalStateException(
            "variantid execution uncertain (${result.reason}); framework start aborted")
    }
}
