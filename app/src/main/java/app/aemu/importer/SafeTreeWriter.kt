package app.aemu.importer

import java.io.File
import java.io.IOException
import java.io.InputStream
import java.nio.file.Files
import java.nio.file.LinkOption
import java.nio.file.StandardOpenOption

/** Bounded writer for an app-private, exclusively owned extraction tree. */
internal class SafeTreeWriter(
    root: File,
    private val maxEntries: Long,
    private val maxBytes: Long,
    private val cancelled: () -> Boolean,
    private val availableBytes: (File) -> Long = { it.usableSpace },
) {
    private val base = root.canonicalFile.toPath()
    private var entries = 0L
    private var bytes = 0L

    init {
        if (Files.isSymbolicLink(root.toPath()) || !Files.isDirectory(base, LinkOption.NOFOLLOW_LINKS))
            throw IOException("Invalid extraction root")
        if (maxEntries < 0 || maxBytes < 0) throw IOException("Invalid extraction limits")
    }

    private fun components(name: String): List<String> {
        if (name.isEmpty() || name.startsWith('/') || name.contains('\\') || name.indexOf('\u0000') >= 0)
            throw IOException("Invalid extraction path")
        val parts = name.split('/')
        if (parts.any { it.isEmpty() || it == "." || it == ".." }) throw IOException("Invalid extraction path")
        return parts
    }

    private fun parent(parts: List<String>): java.nio.file.Path {
        var dir = base
        for (part in parts) {
            dir = dir.resolve(part)
            if (!Files.exists(dir, LinkOption.NOFOLLOW_LINKS)) Files.createDirectory(dir)
            if (!Files.isDirectory(dir, LinkOption.NOFOLLOW_LINKS) || Files.isSymbolicLink(dir))
                throw IOException("Unsafe extraction directory")
        }
        return dir
    }

    fun directory(name: String) {
        if (cancelled()) throw ImportCancelledException()
        val parts = components(name)
        if (entries >= maxEntries) throw IOException("Too many extracted entries")
        parent(parts)
        entries++
    }

    fun write(name: String, size: Long, input: InputStream) {
        if (cancelled()) throw ImportCancelledException()
        val parts = components(name)
        if (entries >= maxEntries) throw IOException("Too many extracted entries")
        if (size < 0 || size > maxBytes - bytes) throw IOException("Extraction size limit exceeded")
        if (availableBytes(base.toFile()) < size) throw IOException("Not enough space to extract firmware")
        val dst = parent(parts.dropLast(1)).resolve(parts.last())
        var created = false
        try {
            Files.newOutputStream(dst, StandardOpenOption.CREATE_NEW, StandardOpenOption.WRITE, LinkOption.NOFOLLOW_LINKS).use { out ->
                created = true
                val buf = ByteArray(1024 * 1024)
                var written = 0L
                input.use { source ->
                    while (true) {
                        if (cancelled()) throw ImportCancelledException()
                        val n = source.read(buf)
                        if (n < 0) break
                        if (n == 0) continue
                        if (n.toLong() > size - written) throw IOException("File exceeds declared size")
                        out.write(buf, 0, n)
                        written += n
                    }
                }
                if (written != size) throw IOException("File shorter than declared size")
            }
            entries++
            bytes += size
        } catch (t: Throwable) {
            if (created) try { Files.deleteIfExists(dst) } catch (cleanup: IOException) { t.addSuppressed(cleanup) }
            throw t
        }
    }
    /** Create links only after every regular file has been extracted and the whole graph is safe. */
    fun links(links: Map<String, String>) {
        if (cancelled()) throw ImportCancelledException()
        if (links.size.toLong() > maxEntries - entries) throw IOException("Too many extracted entries")
        GuestLinks.validate(links)
        for ((name, target) in links) {
            if (cancelled()) throw ImportCancelledException()
            val parts = components(name)
            val dst = parent(parts.dropLast(1)).resolve(parts.last())
            Files.createSymbolicLink(dst, java.nio.file.Paths.get(GuestLinks.hostTarget(name, target)))
            entries++
        }
    }

}
