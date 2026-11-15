package app.aemu.core

import java.io.File
import java.io.ByteArrayInputStream
import java.io.IOException
import java.nio.ByteBuffer
import java.nio.channels.FileChannel
import java.nio.file.Files
import java.nio.file.LinkOption.NOFOLLOW_LINKS
import java.nio.file.OpenOption
import java.nio.file.StandardOpenOption.READ
import java.nio.file.StandardOpenOption.WRITE
import java.nio.file.StandardCopyOption.ATOMIC_MOVE
import java.nio.file.StandardCopyOption.REPLACE_EXISTING
import java.nio.file.attribute.PosixFilePermission
import java.nio.file.attribute.PosixFilePermissions
import java.util.Properties

/** Experimental configuration for a stopped guest. It is not wired to guest startup. */
internal object TargetVirtualWifiProperties {
    private val privatePermissions = setOf(PosixFilePermission.OWNER_READ, PosixFilePermission.OWNER_WRITE)
    private const val NAME = "aemu-wifi.properties"
    private const val LIMIT = 4096L
    private const val HEADER = "# RedMagicEMU virtual Wi-Fi configuration v1\n"

    private fun escaped(value: String): String = buildString {
        for (c in value) when {
            c in " \\:=#!" -> { append('\\'); append(c) }
            c.code !in 0x20..0x7e -> { append("\\u"); append(c.code.toString(16).padStart(4, '0')) }
            else -> append(c)
        }
    }

    /** Properties.load(InputStream) expects ISO-8859-1 with Unicode escapes. */
    fun encode(config: TargetVirtualWifiConfig): ByteArray {
        if (!config.isValid()) throw IOException("Invalid virtual Wi-Fi configuration")
        return buildString {
            append(HEADER)
            append("ssid="); append(escaped(config.ssid)); append('\n')
            append("secured=false\n")
            append("rssi="); append(config.rssiDbm); append('\n')
        }.toByteArray(Charsets.US_ASCII)
    }

    internal fun isOwned(bytes: ByteArray): Boolean = try {
        val p = Properties().apply { load(ByteArrayInputStream(bytes)) }
        val c = TargetVirtualWifiConfig(p.getProperty("ssid"), p.getProperty("rssi").toInt())
        val keys = p.stringPropertyNames()
        if (!c.isValid()) false
        else if (keys == setOf("ssid", "secured", "rssi")) bytes.contentEquals(encode(c))
        else if (keys == setOf("ssid", "secured", "psk", "rssi")) {
            // Recognize the exact old format so an owned secured config can be replaced with an open one.
            val oldPsk = p.getProperty("psk")
            val validOldPsk = oldPsk.isEmpty() ||
                (oldPsk.length in 8..63 && oldPsk.all { it.code in 0x20..0x7e }) ||
                (oldPsk.length == 64 && oldPsk.all { it in '0'..'9' || it in 'a'..'f' || it in 'A'..'F' })
            val legacy = (HEADER + "ssid=" + escaped(c.ssid) + "\nsecured=" + oldPsk.isNotEmpty() +
                "\npsk=" + escaped(oldPsk) + "\nrssi=" + c.rssiDbm + "\n").toByteArray(Charsets.US_ASCII)
            validOldPsk && bytes.contentEquals(legacy)
        } else false
    } catch (_: Exception) { false }

    private fun readOwned(path: java.nio.file.Path): ByteArray {
        if (!Files.isRegularFile(path, NOFOLLOW_LINKS) || Files.size(path) > LIMIT)
            throw IOException("Conflicting Wi-Fi configuration")
        val permissions = try { Files.getPosixFilePermissions(path, NOFOLLOW_LINKS) }
            catch (e: UnsupportedOperationException) { throw IOException("Private Wi-Fi configuration permissions unavailable", e) }
        if (permissions != privatePermissions) throw IOException("Conflicting Wi-Fi configuration permissions")
        val old = Files.newInputStream(path, READ, NOFOLLOW_LINKS).use { input ->
                val data = ByteArray((LIMIT + 1).toInt())
                var count = 0
                while (count < data.size) {
                    val n = input.read(data, count, data.size - count)
                    if (n < 0) break
                    if (n == 0) throw IOException("Cannot read Wi-Fi configuration")
                    count += n
                }
                data.copyOf(count)
        }
        if (!isOwned(old)) throw IOException("Conflicting Wi-Fi configuration")
        return old
    }

    /**
     * Caller must hold the instance's exclusive runtime lease and verify the guest is stopped.
     * Only canonical files marked as ours can be atomically updated. Mode 0600 protects against
     * other host UIDs; guest UID isolation is not established here.
     */
    fun prepareForStoppedGuest(root: File, config: TargetVirtualWifiConfig, beforePublish: () -> Unit = {}) {
        val bytes = encode(config)
        var dir = root.toPath()
        if (!Files.isDirectory(dir, NOFOLLOW_LINKS)) throw IOException("Unsafe Wi-Fi runtime root")
        for (part in listOf("data", "system")) {
            dir = dir.resolve(part)
            if (!Files.isDirectory(dir, NOFOLLOW_LINKS)) throw IOException("Unsafe Wi-Fi runtime directory")
        }
        val path = dir.resolve(NAME)
        val old = if (Files.exists(path, NOFOLLOW_LINKS)) readOwned(path) else null
        if (old != null && old.contentEquals(bytes)) return
        val attrs = PosixFilePermissions.asFileAttribute(privatePermissions)
        val stage = try { Files.createTempFile(dir, ".rme-wifi-", ".tmp", attrs) }
            catch (e: UnsupportedOperationException) { throw IOException("Private Wi-Fi configuration permissions unavailable", e) }
        try {
            FileChannel.open(stage, setOf<OpenOption>(WRITE, NOFOLLOW_LINKS)).use { channel ->
                val buffer = ByteBuffer.wrap(bytes)
                while (buffer.hasRemaining()) if (channel.write(buffer) <= 0) throw IOException("Cannot write Wi-Fi configuration")
                channel.force(true)
            }
            if (Files.getPosixFilePermissions(stage, NOFOLLOW_LINKS) != privatePermissions)
                throw IOException("Unsafe Wi-Fi configuration permissions")
            beforePublish()
            if (old == null) {
                Files.move(stage, path) // no replacement of an entry created since our check
            } else {
                if (!readOwned(path).contentEquals(old)) throw IOException("Concurrent Wi-Fi configuration change")
                Files.move(stage, path, ATOMIC_MOVE, REPLACE_EXISTING)
            }
        } catch (e: UnsupportedOperationException) {
            throw IOException("Private Wi-Fi configuration permissions unavailable", e)
        } finally {
            Files.deleteIfExists(stage)
        }
    }
}
