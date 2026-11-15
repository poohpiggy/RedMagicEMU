package app.aemu.core

import java.io.ByteArrayInputStream
import java.io.File
import java.io.IOException
import java.nio.ByteBuffer
import java.nio.channels.FileChannel
import java.nio.file.Files
import java.nio.file.LinkOption.NOFOLLOW_LINKS
import java.nio.file.StandardCopyOption.ATOMIC_MOVE
import java.nio.file.StandardCopyOption.REPLACE_EXISTING
import java.nio.file.StandardOpenOption.TRUNCATE_EXISTING
import java.nio.file.StandardOpenOption.WRITE
import java.util.zip.ZipInputStream

/** Validates and installs the target ROM's virtual Wi-Fi assets. */
internal object TargetWifiArtifacts {
    const val RUNTIME_READY = true
    private const val ASSET = "engines/redmagic/wifi-service.jar"
    private const val OWNER = "META-INF/redmagic-wifi-owner"
    private const val NETWORK_ASSET = "engines/redmagic/wifi-net-service.jar"
    private const val NETWORK_OWNER = "META-INF/redmagic-wifi-net-owner"
    private const val LIMIT = 32 * 1024 * 1024
    private val ownerBytes = "RedMagicEMU wifi prototype v1\n".toByteArray(Charsets.US_ASCII)
    private val networkOwnerBytes = "RedMagicEMU wifi network prototype v1\n".toByteArray(Charsets.US_ASCII)

    fun applies(api: Int, model: String, abi: String, engine: Engine): Boolean =
        api == 36 && model == "NX809J" && abi == "arm64-v8a" && engine == Engine.N64

    private fun validJar(bytes: ByteArray, ownerEntry: String, expectedOwner: ByteArray): Boolean = try {
        if (bytes.isEmpty() || bytes.size > LIMIT) false else {
            var owner = false
            var dex = false
            var valid = true
            ZipInputStream(ByteArrayInputStream(bytes)).use { zip ->
                while (true) {
                    val entry = zip.nextEntry ?: break
                    when (entry.name) {
                        ownerEntry -> {
                            if (owner || entry.isDirectory) { valid = false; break }
                            val data = ByteArray(expectedOwner.size + 1)
                            var n = 0
                            while (n < data.size) { val count = zip.read(data, n, data.size - n); if (count < 0) break; n += count }
                            owner = n == expectedOwner.size && data.copyOf(n).contentEquals(expectedOwner)
                        }
                        "classes.dex" -> {
                            if (dex || entry.isDirectory) { valid = false; break }
                            val head = ByteArray(8)
                            var n = 0
                            while (n < head.size) { val count = zip.read(head, n, head.size - n); if (count < 0) break; n += count }
                            dex = n == 8 && head[0] == 'd'.code.toByte() && head[1] == 'e'.code.toByte() &&
                                head[2] == 'x'.code.toByte() && head[3] == '\n'.code.toByte() && head[7] == 0.toByte()
                            var total = n
                            val buffer = ByteArray(8192)
                            while (true) {
                                val count = zip.read(buffer)
                                if (count < 0) break
                                total += count
                                if (total > LIMIT) { valid = false; break }
                            }
                        }
                        else -> { valid = false; break }
                    }
                    if (!valid) break
                    zip.closeEntry()
                }
            }
            valid && owner && dex
        }
    } catch (_: IOException) { false }

    fun prepare(root: File, api: Int, model: String, abi: String, engine: Engine,
                enabled: Boolean, readAsset: (String) -> ByteArray) {
        if (!enabled || !applies(api, model, abi, engine)) return
        install(root, ASSET, "redmagic-wifi.jar", OWNER, ownerBytes, readAsset)
    }

    fun prepareNetwork(root: File, api: Int, model: String, abi: String, engine: Engine,
                       enabled: Boolean, readAsset: (String) -> ByteArray) {
        if (!enabled || !applies(api, model, abi, engine)) return
        install(root, NETWORK_ASSET, "redmagic-wifi-net.jar", NETWORK_OWNER, networkOwnerBytes, readAsset)
    }

    private fun install(root: File, asset: String, filename: String, ownerEntry: String,
                        expectedOwner: ByteArray, readAsset: (String) -> ByteArray) {
        var dir = root.toPath()
        if (!Files.isDirectory(dir, NOFOLLOW_LINKS)) throw IOException("Unsafe Wi-Fi runtime root")
        for (part in listOf("system", "framework")) {
            dir = dir.resolve(part)
            if (!Files.isDirectory(dir, NOFOLLOW_LINKS)) throw IOException("Unsafe Wi-Fi framework directory")
        }
        val bytes = readAsset(asset)
        if (!validJar(bytes, ownerEntry, expectedOwner)) throw IOException("Invalid Wi-Fi prototype asset")
        val target = dir.resolve(filename)
        val old = if (Files.exists(target, NOFOLLOW_LINKS)) {
            if (!Files.isRegularFile(target, NOFOLLOW_LINKS) || Files.size(target) > LIMIT) throw IOException("Conflicting Wi-Fi JAR")
            val content = Files.readAllBytes(target)
            if (!validJar(content, ownerEntry, expectedOwner)) throw IOException("Conflicting Wi-Fi JAR")
            content
        } else null
        if (old != null && old.contentEquals(bytes)) return
        val stage = Files.createTempFile(dir, ".rme-wifi-jar-", ".tmp")
        try {
            FileChannel.open(stage, WRITE, TRUNCATE_EXISTING, NOFOLLOW_LINKS).use { channel ->
                val data = ByteBuffer.wrap(bytes)
                while (data.hasRemaining()) if (channel.write(data) <= 0) throw IOException("Cannot write Wi-Fi JAR")
                channel.force(true)
            }
            if (!stage.toFile().setReadable(true, false)) throw IOException("Cannot make Wi-Fi JAR readable")
            if (old == null) Files.move(stage, target)
            else {
                if (!Files.isRegularFile(target, NOFOLLOW_LINKS) || !Files.readAllBytes(target).contentEquals(old))
                    throw IOException("Concurrent Wi-Fi JAR change")
                Files.move(stage, target, ATOMIC_MOVE, REPLACE_EXISTING)
            }
        } finally { Files.deleteIfExists(stage) }
    }
}
