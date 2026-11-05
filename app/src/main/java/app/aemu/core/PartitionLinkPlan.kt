package app.aemu.core

import java.io.File
import java.io.IOException
import java.nio.file.Files
import java.nio.file.LinkOption
import java.nio.file.Path
import java.nio.file.SimpleFileVisitor
import java.nio.file.attribute.BasicFileAttributes

/** Deterministic link targets preserving guest paths across rootLinks partition relocation. */
internal object PartitionLinkPlan {
    data class Rule(val oldPath: String, val newPath: String, val originalTarget: String, val updatedTarget: String)
    fun plan(root: File, name: String, cancelled: () -> Boolean = { false }): List<Rule> {
        if (name !in setOf("product", "system_ext")) throw IOException("invalid partition")
        val base = root.toPath().toAbsolutePath().normalize()
        val source = base.resolve("system").resolve(name)
        if (Files.exists(base.resolve(".rootlinks-$name-txn"), LinkOption.NOFOLLOW_LINKS)) throw IOException("incomplete partition transaction")
        if (!Files.isDirectory(base, LinkOption.NOFOLLOW_LINKS) || !Files.isDirectory(base.resolve("system"), LinkOption.NOFOLLOW_LINKS) || !Files.isDirectory(source, LinkOption.NOFOLLOW_LINKS))
            throw IOException("invalid partition directory")
        val result = ArrayList<Rule>()
        var seen = 0
        Files.walkFileTree(source, object : SimpleFileVisitor<Path>() {
            override fun preVisitDirectory(path: Path, attrs: BasicFileAttributes): java.nio.file.FileVisitResult {
                if (cancelled() || Thread.currentThread().isInterrupted) throw IOException("cancelled")
                return java.nio.file.FileVisitResult.CONTINUE
            }
            override fun visitFile(path: Path, attrs: BasicFileAttributes): java.nio.file.FileVisitResult {
                if (cancelled() || Thread.currentThread().isInterrupted) throw IOException("cancelled")
                if (!attrs.isSymbolicLink) return java.nio.file.FileVisitResult.CONTINUE
                if (++seen > 200_000) throw IOException("too many partition links")
                val old = base.relativize(path).toString().replace(File.separatorChar, '/')
                val new = old.removePrefix("system/")
                val target = Files.readSymbolicLink(path)
                if (target.isAbsolute) throw IOException("absolute host link in partition: $old")
                val text = target.toString().replace(File.separatorChar, '/')
                val pieces = text.split('/')
                val stack = old.split('/').dropLast(1).toMutableList()
                var reachedRoot = false
                for ((i, part) in pieces.withIndex()) {
                    when (part) {
                        "", "." -> Unit
                        ".." -> if (stack.isEmpty()) throw IOException("partition link escapes root: $old") else { stack.removeAt(stack.lastIndex); if (stack.isEmpty()) reachedRoot = true }
                        else -> {
                            stack += part
                            if (i < pieces.lastIndex && pieces.drop(i + 1).contains("..")) {
                                val check = if (stack.firstOrNull() == name) listOf("system") + stack else stack
                                val intermediate = check.fold(base) { p, item -> p.resolve(item) }
                                if (!Files.isDirectory(intermediate, LinkOption.NOFOLLOW_LINKS))
                                    throw IOException("ambiguous or missing partition link target: $old")
                            }
                        }
                    }
                }
                if (stack.isEmpty()) throw IOException("partition link targets root: $old")
                val logical = stack.joinToString("/")
                val adjusted = if (logical == "system/$name" || logical.startsWith("system/$name/")) logical.removePrefix("system/") else logical
                val updated = if (reachedRoot) "../".repeat(new.split('/').size - 1) + adjusted
                    else java.nio.file.Paths.get(new).parent.relativize(java.nio.file.Paths.get(adjusted)).toString().replace(File.separatorChar, '/')
                if (updated != text) result += Rule(old,new,text,updated)
                return java.nio.file.FileVisitResult.CONTINUE
            }
        })
        return result.sortedBy { it.oldPath }
    }
}
