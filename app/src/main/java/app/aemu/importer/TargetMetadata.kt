package app.aemu.importer

import java.util.Properties

/** Metadata observed in the user-designated package's derived ramdisk. */
internal object TargetMetadata {
    const val HISTORY_FINGERPRINT = "REDMAGIC/NX809J/NX809J:16/BQ2A.250705.001-BP2A.250605.031.A3/20260130.143527:user/release-keys"
    fun readFingerprint(entries: List<BootImage.CpioEntry>): String? {
        val entry = entries.singleOrNull { it.name == "system/etc/ramdisk/build.prop" } ?: return null
        return runCatching { Properties().apply { entry.data.inputStream().use { load(it) } }.getProperty("ro.bootimage.build.fingerprint") }.getOrNull()
    }
}
