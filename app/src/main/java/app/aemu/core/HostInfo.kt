package app.aemu.core

/** Сведения о телефоне-хозяине, от которых зависят обходы в движке. */
object HostInfo {
    /** Драйвер GPU (adreno, mali, powervr…) из ro.hardware.egl. */
    fun gpu(): String = runCatching {
        val c = Class.forName("android.os.SystemProperties")
        (c.getMethod("get", String::class.java).invoke(null, "ro.hardware.egl") as String).lowercase()
    }.getOrDefault("")

    /** The cores still execute 32-bit ARM code (Cortex-A76/A78/A55 class): 32-bit programs can run without qemu. */
    fun hasAArch32(): Boolean = android.os.Build.SUPPORTED_32_BIT_ABIS.isNotEmpty()
}
