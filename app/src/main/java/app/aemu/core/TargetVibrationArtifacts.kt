package app.aemu.core

import java.io.File
import java.io.IOException
import java.nio.ByteBuffer
import java.nio.channels.FileChannel
import java.nio.file.FileAlreadyExistsException
import java.nio.file.Files
import java.nio.file.LinkOption.NOFOLLOW_LINKS
import java.nio.file.Path
import java.nio.file.StandardOpenOption.TRUNCATE_EXISTING
import java.nio.file.StandardOpenOption.WRITE

/** Prepare the vibration adapter in the stopped runtime copy only when explicitly enabled. */
internal object TargetVibrationArtifacts {
    private const val ASSET = "engines/redmagic/vibrator-adapter.so"
    private const val NAME = "libredmagic_vibrator.so"
    private const val MAX_SIZE = 8 * 1024 * 1024

    /** True only confirms the selected runtime copy contains the expected asset. */
    fun prepare(root: File, api: Int, model: String, abi: String, engine: Engine,
                enabled: Boolean, readAsset: (String) -> ByteArray): Boolean {
        if (!enabled || api != 36 || model != "NX809J" || abi != "arm64-v8a" || engine != Engine.N64) return false
        var dir = root.toPath()
        if (!Files.isDirectory(dir, NOFOLLOW_LINKS)) throw IOException("Unsafe vibration runtime root")
        val bytes = readAsset(ASSET)
        // ELF64, little-endian, ET_DYN, EM_AARCH64. The asset is emulator-owned.
        if (bytes.size !in 64..MAX_SIZE || bytes[0] != 0x7f.toByte() || bytes[1] != 'E'.code.toByte() ||
            bytes[2] != 'L'.code.toByte() || bytes[3] != 'F'.code.toByte() || bytes[4] != 2.toByte() ||
            bytes[5] != 1.toByte() || bytes[16] != 3.toByte() || bytes[17] != 0.toByte() ||
            bytes[18] != 0xb7.toByte() || bytes[19] != 0.toByte()) throw IOException("Invalid vibration adapter")
        for (part in listOf("vendor", "lib64")) {
            dir = dir.resolve(part)
            if (!Files.exists(dir, NOFOLLOW_LINKS)) {
                try { Files.createDirectory(dir) } catch (_: FileAlreadyExistsException) { }
            }
            if (!Files.isDirectory(dir, NOFOLLOW_LINKS)) throw IOException("Unsafe vibration directory: $part")
        }
        val target = dir.resolve(NAME)
        if (Files.exists(target, NOFOLLOW_LINKS)) { requireSame(target, bytes); return true }
        val stage = Files.createTempFile(dir, ".rme-vibration-", ".tmp")
        try {
            FileChannel.open(stage, WRITE, TRUNCATE_EXISTING, NOFOLLOW_LINKS).use { channel ->
                val source = ByteBuffer.wrap(bytes)
                while (source.hasRemaining()) if (channel.write(source) <= 0) throw IOException("Cannot write vibration adapter")
                channel.force(true)
            }
            if (!stage.toFile().setReadable(true, false)) throw IOException("Cannot make vibration adapter readable")
            // Linking a fully written file publishes it without replacing a concurrently created path.
            try { Files.createLink(target, stage) }
            catch (_: FileAlreadyExistsException) { requireSame(target, bytes) }
        } finally { Files.deleteIfExists(stage) }
        return true
    }

    private fun requireSame(path: Path, bytes: ByteArray) {
        if (!Files.isRegularFile(path, NOFOLLOW_LINKS) || Files.size(path) != bytes.size.toLong() ||
            !Files.readAllBytes(path).contentEquals(bytes)) throw IOException("Conflicting vibration adapter")
    }
}
