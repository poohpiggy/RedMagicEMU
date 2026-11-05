package app.aemu.core

import java.io.File
import java.io.IOException
import java.nio.file.Files
import java.nio.file.LinkOption

/** The clone must not copy a system tree from outside the source guest. */
internal object CloneSafety {
    fun systemDirectory(root: File): File {
        val base = root.toPath()
        if (!Files.isDirectory(base, LinkOption.NOFOLLOW_LINKS)) throw IOException("invalid guest root")
        val realRoot = base.toRealPath()
        val system = base.resolve("system").toRealPath()
        if (!system.startsWith(realRoot) || !Files.isDirectory(system)) throw IOException("system path escapes guest root")
        return system.toFile()
    }
}
