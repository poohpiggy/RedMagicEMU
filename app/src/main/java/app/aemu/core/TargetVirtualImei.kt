package app.aemu.core

import java.io.File
import java.io.IOException
import java.nio.ByteBuffer
import java.nio.channels.FileChannel
import java.nio.file.Files
import java.nio.file.LinkOption.NOFOLLOW_LINKS
import java.nio.file.StandardCopyOption.ATOMIC_MOVE
import java.nio.file.StandardOpenOption.CREATE
import java.nio.file.StandardOpenOption.READ
import java.nio.file.StandardOpenOption.WRITE
import java.security.SecureRandom

/** A stable synthetic identity owned by one admitted instance, never by the host modem. */
internal object TargetVirtualImei {
    private const val NAME = "virtual-imei"
    private val random = SecureRandom()

    fun valid(value: String): Boolean {
        if (value.length != 15 || value.any { it !in '0'..'9' }) return false
        var sum = 0
        for (i in value.indices) {
            var n = value[i] - '0'
            if (i % 2 == 1) { n *= 2; if (n > 9) n -= 9 }
            sum += n
        }
        return sum % 10 == 0
    }

    private fun generate(): String {
        val body = "00" + (1..12).joinToString("") { random.nextInt(10).toString() }
        return body + (0..9).first { valid(body + it) }
    }

    private fun safeDirectory(dir: File) {
        val p = dir.toPath()
        if (Files.isSymbolicLink(p) || !Files.isDirectory(p, NOFOLLOW_LINKS)) throw IOException("Invalid identity directory")
    }

    private fun read(path: java.nio.file.Path): String {
        if (!Files.isRegularFile(path, NOFOLLOW_LINKS) || Files.size(path) != 15L) throw IOException("Invalid virtual identity")
        val data = Files.newInputStream(path, READ, NOFOLLOW_LINKS).use { input ->
            val buffer = ByteArray(16)
            var used = 0
            while (used < buffer.size) {
                val n = input.read(buffer, used, buffer.size - used)
                if (n < 0) break
                if (n == 0) throw IOException("Cannot read virtual identity")
                used += n
            }
            buffer.copyOf(used)
        }
        val value = String(data, Charsets.US_ASCII)
        if (data.size != 15 || !valid(value)) throw IOException("Invalid virtual identity")
        return value
    }

    /** Persist before use. An invalid or conflicting prior value is never silently replaced. */
    @Synchronized fun ensure(dir: File, preferred: String): String {
        safeDirectory(dir)
        if (preferred.isNotEmpty() && !valid(preferred)) throw IOException("Invalid configured virtual identity")
        val base = dir.toPath()
        val lockPath = base.resolve(".virtual-imei.lock")
        FileChannel.open(lockPath, CREATE, WRITE, NOFOLLOW_LINKS).use { lockChannel ->
            lockChannel.lock().use {
                val path = base.resolve(NAME)
                if (Files.exists(path, NOFOLLOW_LINKS)) {
                    val existing = read(path)
                    if (preferred.isNotEmpty() && preferred != existing) throw IOException("Conflicting virtual identity")
                    return existing
                }
                val value = preferred.ifEmpty { generate() }
                val temporary = Files.createTempFile(base, ".virtual-imei-", ".tmp")
                try {
                    FileChannel.open(temporary, WRITE, NOFOLLOW_LINKS).use { channel ->
                        val bytes = ByteBuffer.wrap(value.toByteArray(Charsets.US_ASCII))
                        while (bytes.hasRemaining()) channel.write(bytes)
                        channel.force(true)
                    }
                    Files.move(temporary, path, ATOMIC_MOVE)
                } finally { Files.deleteIfExists(temporary) }
                return read(path)
            }
        }
    }

    /** Present an instance-local copy at a fixed guest path, never an actual hardware node. */
    fun expose(root: File, value: String, beforePublish: () -> Unit = {}) {
        safeDirectory(root)
        if (!valid(value)) throw IOException("Invalid virtual identity")
        var dir = root.toPath()
        for (part in listOf("data", "vendor", "radio")) {
            dir = dir.resolve(part)
            if (!Files.exists(dir, NOFOLLOW_LINKS)) Files.createDirectory(dir)
            if (Files.isSymbolicLink(dir) || !Files.isDirectory(dir, NOFOLLOW_LINKS)) throw IOException("Invalid virtual identity directory")
        }
        val path = dir.resolve("aemu-imei")
        if (Files.exists(path, NOFOLLOW_LINKS)) {
            if (read(path) != value) throw IOException("Conflicting virtual identity")
            return
        }
        val temporary = Files.createTempFile(dir, ".aemu-imei-", ".tmp")
        try {
            FileChannel.open(temporary, WRITE, NOFOLLOW_LINKS).use { channel ->
                val bytes = ByteBuffer.wrap(value.toByteArray(Charsets.US_ASCII))
                while (bytes.hasRemaining()) channel.write(bytes)
                channel.force(true)
            }
            beforePublish()
            // No REPLACE_EXISTING: a conflicting path must never be overwritten.
            Files.move(temporary, path)
        } finally { Files.deleteIfExists(temporary) }
    }
}
