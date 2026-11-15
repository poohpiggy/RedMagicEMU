package app.aemu.core

import java.io.ByteArrayInputStream
import java.io.File
import java.io.IOException
import java.nio.file.Files
import java.nio.file.LinkOption.NOFOLLOW_LINKS
import java.nio.file.StandardCopyOption.ATOMIC_MOVE
import java.nio.file.StandardCopyOption.REPLACE_EXISTING
import java.util.zip.ZipInputStream

/** Target-only VoldStub override; the shared guest services JAR remains untouched. */
internal object TargetVoldArtifacts {
    private const val ASSET = "engines/redmagic/vold-service.jar"
    private const val OWNER = "META-INF/redmagic-vold-owner"
    private val marker = "RedMagicEMU vold v1\n".toByteArray(Charsets.US_ASCII)
    private const val LIMIT = 2 * 1024 * 1024

    private fun valid(bytes: ByteArray): Boolean = try {
        if (bytes.isEmpty() || bytes.size > LIMIT) false else {
            var owner = false; var dex = false; var sawOwner = false; var sawDex = false; var ok = true
            ZipInputStream(ByteArrayInputStream(bytes)).use { z ->
                while (true) {
                    val e = z.nextEntry ?: break
                    when (e.name) {
                        OWNER -> {
                            if (sawOwner || e.isDirectory) { ok = false; break }
                            sawOwner = true
                            val b = ByteArray(marker.size + 1); var n = 0
                            while (n < b.size) { val k = z.read(b, n, b.size - n); if (k < 0) break; n += k }
                            owner = n == marker.size && b.copyOf(n).contentEquals(marker)
                        }
                        "classes.dex" -> {
                            if (sawDex || e.isDirectory) { ok = false; break }
                            sawDex = true
                            val b = ByteArray(8); var n = 0
                            while (n < b.size) { val k = z.read(b, n, b.size - n); if (k < 0) break; n += k }
                            dex = n == 8 && b[0] == 100.toByte() && b[1] == 101.toByte() && b[2] == 120.toByte() && b[3] == 10.toByte() && b[7] == 0.toByte()
                            var total = n; val scratch = ByteArray(8192)
                            while (true) { val k = z.read(scratch); if (k < 0) break; total += k; if (total > LIMIT) { ok = false; break } }
                        }
                        else -> { ok = false; break }
                    }
                    if (!ok) break
                    z.closeEntry()
                }
            }
            ok && owner && dex
        }
    } catch (_: IOException) { false }

    fun prepare(root: File, api: Int, model: String, abi: String, engine: Engine, readAsset: (String) -> ByteArray): Boolean {
        if (api != 36 || model != "NX809J" || abi != "arm64-v8a" || engine != Engine.N64) return false
        var dir = root.toPath()
        if (!Files.isDirectory(dir, NOFOLLOW_LINKS)) throw IOException("Unsafe vold runtime root")
        for (part in listOf("system", "framework")) {
            dir = dir.resolve(part)
            if (!Files.isDirectory(dir, NOFOLLOW_LINKS)) throw IOException("Unsafe vold framework directory")
        }
        val bytes = readAsset(ASSET)
        if (!valid(bytes)) throw IOException("Invalid vold asset")
        val target = dir.resolve("redmagic-vold.jar")
        val old = if (Files.exists(target, NOFOLLOW_LINKS)) {
            if (!Files.isRegularFile(target, NOFOLLOW_LINKS) || Files.size(target) > LIMIT) throw IOException("Conflicting vold JAR")
            Files.readAllBytes(target).also { if (!valid(it)) throw IOException("Conflicting vold JAR") }
        } else null
        if (old != null && old.contentEquals(bytes)) return true
        val stage = Files.createTempFile(dir, ".rme-vold-", ".tmp")
        try {
            Files.write(stage, bytes)
            if (!stage.toFile().setReadable(true, false)) throw IOException("Cannot make vold JAR readable")
            if (old == null) Files.move(stage, target)
            else {
                if (!Files.isRegularFile(target, NOFOLLOW_LINKS) || !Files.readAllBytes(target).contentEquals(old)) throw IOException("Concurrent vold JAR change")
                Files.move(stage, target, ATOMIC_MOVE, REPLACE_EXISTING)
            }
        } finally { Files.deleteIfExists(stage) }
        return true
    }

    /** Classpath and mode must be selected together: an old JAR does not understand userstart36. */
    fun launch(prepared: Boolean, userStart: Boolean): List<String> {
        val jars = (if (prepared) "/system/framework/redmagic-vold.jar:" else "") + "/system/framework/aemu-stubs.jar"
        return listOf("-Djava.class.path=$jars") + if (prepared) listOf("userstart36") else if (userStart) listOf("userstart") else emptyList()
    }
}
