package app.aemu.core

import java.io.File
import java.nio.file.Files
import java.nio.file.LinkOption

/** Admission compares build fingerprint values; it is not a package integrity check. */
object InstanceAdmission {
    const val SYSTEM_FINGERPRINT = "REDMAGIC/NX809J/NX809J:16/BQ2A.250705.001-BP2A.250605.031.A3/20260130.143527:user/release-keys"
    const val BOOT_FINGERPRINT = "REDMAGIC/NX809J/NX809J:16/BQ2A.250705.001-BP2A.250605.031.A3/20260130.145957:user/release-keys"
    private val allowed = setOf(SYSTEM_FINGERPRINT, BOOT_FINGERPRINT)
    private val keys = setOf("ro.build.fingerprint", "ro.system.build.fingerprint", "ro.bootimage.build.fingerprint")

    /** Use the saved value if present; otherwise read the system properties for older instances. */
    fun requireVerified(instanceDir: File, storedFingerprint: String? = null) {
        val images = instanceDir.absoluteFile.parentFile ?: error("Invalid instance directory")
        app.aemu.importer.PendingImports.withImagesLock(images) {
            val id = instanceDir.name
            check(ImageId.valid(id)) { "Invalid instance ID" }
            val candidates = Files.newDirectoryStream(images.toPath()).use { entries ->
                entries.filter { entry ->
                    ImageId.valid(entry.fileName.toString()) &&
                        (Files.exists(entry.resolve("image.json"), LinkOption.NOFOLLOW_LINKS) ||
                         Files.exists(entry.resolve(".redmagic-import-pending"), LinkOption.NOFOLLOW_LINKS))
                }.map { it.fileName.toString() }.toList()
            }
            check(candidates.size == 1 && candidates.single() == id) {
                "Multiple or unavailable instances: resolve the conflict before starting"
            }
            requireFingerprint(storedFingerprint ?: readFingerprint(instanceDir))
        }
    }

    fun requireFingerprint(value: String?) {
        val normalized = value?.trim()
        if (normalized.isNullOrEmpty()) error("此版本仅支持指定的 REDMAGIC NX809J 固件。 Firmware fingerprint is missing")
        if (normalized !in allowed) error("此版本仅支持指定的 REDMAGIC NX809J 固件。 Firmware fingerprint does not match the target")
    }

    /** Additional candidates come from the same package's bootimage metadata during import. */
    fun readFingerprint(instanceDir: File, additionalCandidates: List<String> = emptyList()): String? {
        val root = instanceDir.toPath().resolve("root")
        val system = root.resolve("system")
        val build = system.resolve("build.prop")
        val canRead = Files.isDirectory(instanceDir.toPath(), LinkOption.NOFOLLOW_LINKS) &&
            Files.isDirectory(root, LinkOption.NOFOLLOW_LINKS) &&
            Files.isDirectory(system, LinkOption.NOFOLLOW_LINKS) &&
            Files.isRegularFile(build, LinkOption.NOFOLLOW_LINKS) && Files.size(build) <= 1024 * 1024
        val candidates = if (canRead) Files.newBufferedReader(build, Charsets.UTF_8).useLines { lines ->
            lines.mapNotNull { line ->
                val at = line.indexOf('=')
                if (at > 0 && line.substring(0, at).trim() in keys) line.substring(at + 1).trim() else null
            }.toList()
        } else emptyList()
        val all = candidates + additionalCandidates.map { it.trim() }
        return all.firstOrNull { it in allowed } ?: all.firstOrNull { it.isNotEmpty() }
    }
}
