package app.aemu.core

import java.io.File
import java.io.IOException
import java.nio.file.Files
import java.nio.file.LinkOption

/** Relative APEX target based on the real guest location after partition moves. */
internal object ApexLink {
    fun relativeTarget(root: File, sourceDirectory: File, module: String): String {
        if (!module.matches(Regex("[A-Za-z0-9_.]+")) || module == "." || module == "..") throw IOException("invalid APEX module")
        if (!Files.isDirectory(root.toPath(), LinkOption.NOFOLLOW_LINKS)) throw IOException("unsafe guest root")
        val base = root.toPath().toRealPath()
        val source = sourceDirectory.toPath().toRealPath()
        val apex = base.resolve("apex")
        if (!source.startsWith(base) || !Files.isDirectory(apex, LinkOption.NOFOLLOW_LINKS)) throw IOException("unsafe APEX link location")
        return source.relativize(apex.resolve(module)).toString().replace(File.separatorChar, '/')
    }
}
