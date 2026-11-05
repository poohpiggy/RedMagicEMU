package app.aemu.core

import java.io.File
import java.nio.file.Files
import java.nio.file.LinkOption

/** Preserve a parked firmware entry or fail the import instead of silently skipping it. */
internal object SanitizeMove {
    fun park(root: File, parked: File, source: File): Boolean {
        if (!Files.exists(source.toPath(), LinkOption.NOFOLLOW_LINKS)) return false
        val rel = source.relativeTo(root).path.replace(File.separatorChar, '#').replace('/', '#')
        val dst = File(parked, rel)
        if (Files.exists(dst.toPath(), LinkOption.NOFOLLOW_LINKS)) Files.delete(dst.toPath())
        Files.move(source.toPath(), dst.toPath())
        return true
    }
}

