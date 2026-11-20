package app.aemu.core

import java.io.File
import java.io.InputStream
import java.nio.file.Files
import java.nio.file.StandardCopyOption.ATOMIC_MOVE
import java.security.MessageDigest

/** Replaces the two admitted runtime Codec2 libraries while the host video bridge is available. */
internal class TargetVideoRuntime(private val root: File, private val asset: (String) -> InputStream) {
    companion object {
        const val INDEX = "rme-video/guest.idx"
        const val EXECUTABLE = "libaemuvideohost.so"
        const val SOCKET = "rme-video-test"
        private val originals = mapOf(
            "avc" to "d414513d34791c89b61a40dbef6c0a7e7f26a554cb1d5b9079a2860550cb21f9",
            "hevc" to "e16488f4ae238f65f782bf9ea8432710b5cc87fcf837c16ee653080c4c4ed37a",
        )
    }

    private val libraryDir = File(root, "apex/com.android.media.swcodec/lib64")

    fun install() {
        restore()
        val hashes = asset(INDEX).bufferedReader().use { reader ->
            reader.readLines().filter(String::isNotBlank).associate { line ->
                val fields = line.split('\t')
                require(fields.size == 2) { "invalid Guest video bundle index" }
                fields[0] to fields[1]
            }
        }
        require(hashes.keys == originals.keys) { "incomplete Guest video bundle" }
        for ((codec, expected) in originals) {
            val original = library(codec)
            require(original.isFile && !Files.isSymbolicLink(original.toPath()) && sha256(original) == expected) {
                "unsupported Guest $codec codec library"
            }
        }
        try {
            hashes.forEach { (codec, expected) ->
                val candidate = File.createTempFile(".rme-video-", ".so", libraryDir)
                try {
                    asset("rme-video/$codec.so").use { source ->
                        candidate.outputStream().use { source.copyTo(it) }
                    }
                    check(sha256(candidate) == expected) { "Guest video asset mismatch for $codec" }
                    Files.move(library(codec).toPath(), backup(codec).toPath(), ATOMIC_MOVE)
                    Files.move(candidate.toPath(), library(codec).toPath(), ATOMIC_MOVE)
                } finally {
                    candidate.delete()
                }
            }
        } catch (failure: Exception) {
            runCatching { restore() }.onFailure(failure::addSuppressed)
            throw failure
        }
    }

    fun restore() {
        originals.forEach { (codec, expected) ->
            val original = backup(codec)
            if (original.exists()) {
                check(!Files.isSymbolicLink(original.toPath()) && original.isFile && sha256(original) == expected) {
                    "Guest video backup mismatch for $codec"
                }
                Files.move(original.toPath(), library(codec).toPath(), ATOMIC_MOVE,
                    java.nio.file.StandardCopyOption.REPLACE_EXISTING)
            }
        }
    }

    private fun library(codec: String) = File(libraryDir, "libcodec2_soft_${codec}dec.so")
    private fun backup(codec: String) = File(libraryDir, "libcodec2_soft_${codec}dec.rme-original.so")

    private fun sha256(path: File): String = MessageDigest.getInstance("SHA-256")
        .digest(path.readBytes()).joinToString("") { "%02x".format(it) }
}
