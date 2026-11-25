package app.aemu.core

import java.io.File
import java.io.IOException
import java.nio.file.FileAlreadyExistsException
import java.nio.file.Files
import java.nio.file.LinkOption
import java.nio.file.Path

/** Bounds instance operations to an app-owned, non-symlink directory and path. */
internal object InstancePathSafety {
    fun prepareImportImagesDir(filesDir: Path, appDataDir: Path): File {
        val files = resolveHostAppDataPath(filesDir.toAbsolutePath().normalize(), appDataDir)
        invalidDirectoryComponent(files)?.let { throw IOException("Invalid import directory: ${it.fileName}") }

        val images = files.resolve("images")
        try {
            Files.createDirectory(images)
        } catch (_: FileAlreadyExistsException) {
            // An existing entry must pass the same directory check as a new one.
        }
        if (!Files.isDirectory(images, LinkOption.NOFOLLOW_LINKS))
            throw IOException("Invalid import directory: images")
        return images.toFile()
    }

    fun resolveHostAppDataPath(path: Path, appDataDir: Path?): Path {
        val appData = appDataDir?.toAbsolutePath()?.normalize() ?: return path
        if (!path.startsWith(appData.resolve("files"))) return path
        val systemParent = appData.parent ?: return path
        // Resolve Android's storage aliases above the app directory. Check the
        // app directory and its descendants separately without following links.
        return systemParent.toRealPath().resolve(systemParent.relativize(path))
    }

    fun invalidDirectoryComponent(absolute: Path): Path? {
        var current = absolute.root ?: throw IOException("Missing directory root")
        for (part in absolute) {
            current = current.resolve(part)
            if (!Files.isDirectory(current, LinkOption.NOFOLLOW_LINKS)) return current
        }
        return null
    }

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
