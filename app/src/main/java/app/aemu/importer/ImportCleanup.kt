package app.aemu.importer

import java.io.File
import java.io.IOException
import java.nio.file.Files
import java.nio.file.LinkOption
import java.nio.file.Path
import java.nio.file.SimpleFileVisitor
import java.nio.file.attribute.BasicFileAttributes

/** Delete only the newly-created instance, without traversing its guest links. */
internal object ImportCleanup {
    fun remove(instance: File) {
        val root = instance.toPath()
        if (!Files.exists(root, LinkOption.NOFOLLOW_LINKS)) return
        if (Files.isSymbolicLink(root) || !Files.isDirectory(root, LinkOption.NOFOLLOW_LINKS))
            throw IOException("Invalid incomplete instance directory")
        Files.walkFileTree(root, object : SimpleFileVisitor<Path>() {
            override fun preVisitDirectory(dir: Path, attrs: BasicFileAttributes): java.nio.file.FileVisitResult {
                if (!dir.toFile().setWritable(true, true) && !Files.isWritable(dir)) throw IOException("Cannot clean instance directory")
                return java.nio.file.FileVisitResult.CONTINUE
            }
            override fun visitFile(file: Path, attrs: BasicFileAttributes): java.nio.file.FileVisitResult {
                Files.delete(file)
                return java.nio.file.FileVisitResult.CONTINUE
            }
            override fun postVisitDirectory(dir: Path, exc: IOException?): java.nio.file.FileVisitResult {
                if (exc != null) throw exc
                Files.delete(dir)
                return java.nio.file.FileVisitResult.CONTINUE
            }
        })
    }
}
