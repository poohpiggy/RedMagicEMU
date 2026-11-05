package app.aemu.core

import java.io.File
import java.nio.file.Files
import java.nio.file.LinkOption

/** Bounds instance operations to an app-owned, non-symlink directory and path. */
internal object InstancePathSafety {
    fun safeInstance(base: File, id: String): Boolean {
        if (!ImageId.valid(id)) return false
        val dir = File(base, id)
        return runCatching { !Files.isSymbolicLink(dir.toPath()) &&
            Files.isDirectory(dir.toPath(), LinkOption.NOFOLLOW_LINKS) &&
            dir.canonicalFile.parentFile == base.canonicalFile }.getOrDefault(false)
    }

    fun safeRelative(instance: File, relative: String): Boolean {
        val parts = relative.split('/')
        if (parts.any { it.isEmpty() || it == "." || it == ".." || it.contains('\\') }) return false
        var current = instance.toPath()
        for (part in parts) {
            current = current.resolve(part)
            if (Files.isSymbolicLink(current)) return false
        }
        return true
    }
}
