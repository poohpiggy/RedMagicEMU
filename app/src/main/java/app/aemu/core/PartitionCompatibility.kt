package app.aemu.core

import java.io.File
import java.io.IOException
import java.nio.file.Files
import java.nio.file.LinkOption
import java.security.MessageDigest

/** The two exact rootLinks directory moves used by the API 36 engine. */
internal object PartitionCompatibility {
    data class Rule(val name: String, val originalTarget: ByteArray)
    data class State(val name: String, val moved: Boolean)

    fun matchesOriginal(path: java.nio.file.Path, expected: ByteArray): Boolean = try {
        expected.size == 32 && MessageDigest.isEqual(fingerprint(path), expected)
    } catch (_: IOException) { false }

    fun plan(root: File): List<Rule> {
        val system = File(root, "system").toPath()
        if (!Files.exists(system, LinkOption.NOFOLLOW_LINKS)) return emptyList()
        if (!Files.isDirectory(system, LinkOption.NOFOLLOW_LINKS)) throw IOException("unsafe system directory")
        return listOf("product", "system_ext").mapNotNull { name ->
            val source = system.resolve(name)
            if (!Files.isDirectory(source, LinkOption.NOFOLLOW_LINKS)) return@mapNotNull null
            val dest = root.toPath().resolve(name)
            if (Files.isDirectory(dest, LinkOption.NOFOLLOW_LINKS) && Files.newDirectoryStream(dest).use { it.iterator().hasNext() }) return@mapNotNull null
            if (Files.exists(dest, LinkOption.NOFOLLOW_LINKS) && !Files.isDirectory(dest, LinkOption.NOFOLLOW_LINKS) && !Files.isSymbolicLink(dest)) return@mapNotNull null
            Rule(name, fingerprint(dest))
        }
    }

    fun state(root: File, rule: Rule): State? {
        if (rule.name !in setOf("product", "system_ext") || rule.originalTarget.size != 32) return null
        val base = root.toPath()
        val system = base.resolve("system")
        if (!Files.isDirectory(base, LinkOption.NOFOLLOW_LINKS) || !Files.isDirectory(system, LinkOption.NOFOLLOW_LINKS)) return null
        val source = system.resolve(rule.name)
        val dest = root.toPath().resolve(rule.name)
        if (Files.isDirectory(source, LinkOption.NOFOLLOW_LINKS) && MessageDigest.isEqual(fingerprint(dest), rule.originalTarget))
            return State(rule.name, false)
        if (Files.isSymbolicLink(source) && Files.isDirectory(dest, LinkOption.NOFOLLOW_LINKS) &&
            Files.readSymbolicLink(source).toString().replace(File.separatorChar, '/') == "../${rule.name}")
            return State(rule.name, true)
        return null
    }

    private fun fingerprint(path: java.nio.file.Path): ByteArray {
        val hash = MessageDigest.getInstance("SHA-256")
        when {
            !Files.exists(path, LinkOption.NOFOLLOW_LINKS) -> hash.update(0)
            Files.isSymbolicLink(path) -> {
                hash.update(1); hash.update(Files.readSymbolicLink(path).toString().toByteArray(Charsets.UTF_8))
            }
            Files.isDirectory(path, LinkOption.NOFOLLOW_LINKS) -> {
                if (Files.newDirectoryStream(path).use { it.iterator().hasNext() }) throw IOException("partition placeholder is not empty")
                hash.update(2)
                val m = try { (Files.getAttribute(path, "unix:mode", LinkOption.NOFOLLOW_LINKS) as Number).toInt() and 0xfff }
                    catch (_: UnsupportedOperationException) { -1 } catch (_: IllegalArgumentException) { -1 }
                hash.update(byteArrayOf((m shr 24).toByte(), (m shr 16).toByte(), (m shr 8).toByte(), m.toByte()))
            }
            else -> throw IOException("invalid partition placeholder")
        }
        return hash.digest()
    }
}
