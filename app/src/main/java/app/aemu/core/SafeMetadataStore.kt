package app.aemu.core

import java.io.File
import java.io.IOException
import java.nio.ByteBuffer
import java.nio.channels.FileChannel
import java.nio.file.Files
import java.nio.file.LinkOption
import java.nio.file.StandardCopyOption
import java.nio.file.StandardOpenOption

/** Write metadata through an exclusively created file, never through an existing temporary path. */
internal object SafeMetadataStore {
    fun write(dir: File, content: ByteArray) {
        val parent = dir.toPath()
        if (!Files.isDirectory(parent, LinkOption.NOFOLLOW_LINKS)) throw IOException("Invalid instance directory")
        val target = parent.resolve("image.json")
        if (Files.exists(target, LinkOption.NOFOLLOW_LINKS) && !Files.isRegularFile(target, LinkOption.NOFOLLOW_LINKS))
            throw IOException("Invalid instance metadata")
        val temp = Files.createTempFile(parent, ".image-", ".tmp")
        var failure: Throwable? = null
        try {
            FileChannel.open(temp, StandardOpenOption.WRITE, LinkOption.NOFOLLOW_LINKS).use { output ->
                val bytes = ByteBuffer.wrap(content)
                while (bytes.hasRemaining()) output.write(bytes)
                output.force(true)
            }
            Files.move(temp, target, StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING)
        } catch (t: Throwable) {
            failure = t
            throw t
        } finally {
            try { Files.deleteIfExists(temp) } catch (cleanup: IOException) {
                if (failure != null) failure.addSuppressed(cleanup) else throw cleanup
            }
        }
    }
}
