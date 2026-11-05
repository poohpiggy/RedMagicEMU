package app.aemu.core

import java.io.File
import java.io.IOException
import java.nio.ByteBuffer
import java.nio.channels.FileChannel
import java.nio.file.Files
import java.nio.file.LinkOption
import java.nio.file.NoSuchFileException
import java.nio.file.StandardCopyOption
import java.nio.file.StandardOpenOption
import java.nio.file.attribute.BasicFileAttributes

internal class PowerRequestHistory(private val directory: File) {
    private val path get() = directory.toPath().resolve("last-guest-power-request")

    fun record(request: String) = synchronized(lock) {
        val bytes = request.toByteArray(Charsets.UTF_8)
        require(bytes.isNotEmpty() && bytes.size <= MAX_REQUEST_BYTES) { "Invalid guest power request" }
        val parent = directory.toPath()
        if (attributes(parent)?.isDirectory != true) throw IOException("Invalid VM run directory")
        if (attributes(path)?.isRegularFile == false)
            throw IOException("Invalid guest power request history")
        val temp = Files.createTempFile(parent, ".guest-power-", ".tmp")
        var failure: Throwable? = null
        try {
            FileChannel.open(temp, StandardOpenOption.WRITE, LinkOption.NOFOLLOW_LINKS).use { output ->
                val buffer = ByteBuffer.wrap(bytes)
                while (buffer.hasRemaining()) output.write(buffer)
                output.force(true)
            }
            Files.move(temp, path, StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING)
        } catch (t: Throwable) {
            failure = t
            throw t
        } finally {
            try { Files.deleteIfExists(temp) } catch (cleanup: IOException) {
                if (failure != null) failure.addSuppressed(cleanup) else throw cleanup
            }
        }
    }

    fun read(): String? = synchronized(lock) {
        val parent = directory.toPath()
        val parentAttributes = attributes(parent) ?: return null
        if (!parentAttributes.isDirectory) throw IOException("Invalid VM run directory")
        val fileAttributes = attributes(path) ?: return null
        if (!fileAttributes.isRegularFile) throw IOException("Invalid guest power request history")
        FileChannel.open(path, StandardOpenOption.READ, LinkOption.NOFOLLOW_LINKS).use { input ->
            val size = input.size()
            if (size <= 0 || size > MAX_REQUEST_BYTES) throw IOException("Invalid guest power request length")
            val bytes = ByteArray(size.toInt())
            val buffer = ByteBuffer.wrap(bytes)
            while (buffer.hasRemaining()) {
                if (input.read(buffer) < 0) throw IOException("Truncated guest power request history")
            }
            return Charsets.UTF_8.newDecoder().decode(ByteBuffer.wrap(bytes)).toString()
        }
    }

    private fun attributes(path: java.nio.file.Path): BasicFileAttributes? =
        try { Files.readAttributes(path, BasicFileAttributes::class.java, LinkOption.NOFOLLOW_LINKS) }
        catch (_: NoSuchFileException) { null }

    private companion object {
        const val MAX_REQUEST_BYTES = 65535
        val lock = Any()
    }
}
