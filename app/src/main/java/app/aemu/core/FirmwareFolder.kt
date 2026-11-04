package app.aemu.core

import android.os.Environment
import java.io.File

/**
 * Internal storage/RedMagicEMU/firmware: firmware files dropped there show up in the library for one-tap import.
 * Next to the systems' shared memory card folders.
 */
object FirmwareFolder {
    private val EXT = Regex("""(?i).*\.(zip|7z|rar|tar|md5|tgz|gz|xz|bz2|tbz2?|lz4|br|img|win|ext4|pac|ftf|sin|bin|iso|yaffs2|rfs|simg|ozip|ofp|kdz)$""")

    val dir: File get() = File(Environment.getExternalStorageDirectory(), "RedMagicEMU/firmware")

    /** Firmware files, newest first; empty when there is no all-files access. */
    fun list(): List<File> = runCatching {
        if (!Environment.isExternalStorageManager()) return emptyList()
        dir.mkdirs()
        dir.listFiles().orEmpty().filter { it.isFile && it.length() > 1_000_000 && EXT.matches(it.name) }
            .sortedByDescending { it.lastModified() }
    }.getOrDefault(emptyList())
}
