package app.aemu.core

import java.io.File
import java.io.IOException
import java.nio.file.Files
import java.nio.file.LinkOption
import java.util.ArrayDeque

/** Resolve only link metadata, refusing any link chain that can escape the guest root. */
internal object GuestPathSafety {
    fun contained(root: File, linkName: String, target: String): Boolean {
        return try {
        if (!Files.isDirectory(root.toPath(), LinkOption.NOFOLLOW_LINKS)) return false
        val base = root.toPath().toRealPath()
        if (!Files.isDirectory(base, LinkOption.NOFOLLOW_LINKS)) return false
        val parts = linkName.split('/')
        if (parts.isEmpty() || parts.any { it.isEmpty() || it == "." || it == ".." || it.contains('\\') }) return false
        if (target.isEmpty() || target.contains('\\') || target.indexOf('\u0000') >= 0) return false
        var parent = base
        for (part in parts.dropLast(1)) {
            parent = parent.resolve(part)
            if (!Files.isDirectory(parent, LinkOption.NOFOLLOW_LINKS)) return false
        }
        val stack = parts.dropLast(1).toMutableList()
        if (target.startsWith('/')) stack.clear() // an original guest-absolute target
        val queue = ArrayDeque<String>()
        target.split('/').forEach(queue::addLast)
        var followed = 0
        while (queue.isNotEmpty()) {
            when (val part = queue.removeFirst()) {
                "", "." -> Unit
                ".." -> if (stack.isEmpty()) return false else stack.removeAt(stack.lastIndex)
                else -> {
                    stack.add(part)
                    if (stack.size == 1 && stack[0] in setOf("data", "dev", "proc", "sys", "cache")) return false
                    val path = stack.fold(base) { p, name -> p.resolve(name) }
                    if (Files.isSymbolicLink(path)) {
                        if (++followed > 256) return false
                        val next = Files.readSymbolicLink(path)
                        stack.removeAt(stack.lastIndex)
                        val text = if (next.isAbsolute) {
                            val absolute = next.normalize()
                            if (!absolute.startsWith(base)) return false
                            stack.clear()
                            base.relativize(absolute).toString()
                        } else next.toString()
                        text.replace(File.separatorChar, '/').split('/').asReversed().forEach(queue::addFirst)
                    } else if (queue.isNotEmpty() && Files.exists(path, LinkOption.NOFOLLOW_LINKS) &&
                        !Files.isDirectory(path, LinkOption.NOFOLLOW_LINKS)) return false
                }
            }
        }
        stack.isNotEmpty()
        } catch (_: IOException) { false } catch (_: IllegalArgumentException) { false }
    }
    /** Check links planned for a future APEX tree against the current authenticated tree. */
    fun prospective(root: File, linkName: String, target: String, virtualLinks: Map<String, String>, moved: Set<String>, rejectMutable: Boolean = true): Boolean {
        return try {
            if (!Files.isDirectory(root.toPath(), LinkOption.NOFOLLOW_LINKS)) return false
        val base = root.toPath().toRealPath()
            if (!Files.isDirectory(base, LinkOption.NOFOLLOW_LINKS)) return false
            val parts = linkName.split('/')
            if (parts.any { it.isEmpty() || it == "." || it == ".." || it.contains('\\') }) return false
            if (target.isEmpty() || target.contains('\\') || target.indexOf('\u0000') >= 0) return false
            val stack = parts.dropLast(1).toMutableList()
            if (target.startsWith('/')) stack.clear()
            val queue = ArrayDeque<String>()
            target.split('/').forEach(queue::addLast)
            var followed = 0
            while (queue.isNotEmpty()) {
                when (val part = queue.removeFirst()) {
                    "", "." -> Unit
                    ".." -> if (stack.isEmpty()) return false else stack.removeAt(stack.lastIndex)
                    else -> {
                        stack.add(part)
                        if (rejectMutable && stack.size == 1 && stack[0] in setOf("data", "dev", "proc", "sys", "cache")) return false
                        val logical = stack.joinToString("/")
                        val physical = when {
                            "system_ext" in moved && (logical == "system_ext" || logical.startsWith("system_ext/")) -> "system/$logical"
                            "product" in moved && (logical == "product" || logical.startsWith("product/")) -> "system/$logical"
                            else -> logical
                        }
                        val path = base.resolve(physical)
                        val planned = virtualLinks[logical]
                        val existing = if (planned == null && Files.isSymbolicLink(path)) Files.readSymbolicLink(path) else null
                        val absoluteExisting = existing?.isAbsolute == true
                        val next = if (absoluteExisting) {
                            val absolute = existing!!.normalize()
                            if (!absolute.startsWith(base)) return false
                            base.relativize(absolute).toString().replace(File.separatorChar, '/')
                        } else planned ?: existing?.toString()?.replace(File.separatorChar, '/')
                        if (next != null) {
                            if (++followed > 256 || next.isEmpty() || next.contains('\\')) return false
                            stack.removeAt(stack.lastIndex)
                            if (absoluteExisting || next.startsWith('/')) stack.clear()
                            next.split('/').asReversed().forEach(queue::addFirst)
                        } else if (queue.isNotEmpty() && Files.exists(path, LinkOption.NOFOLLOW_LINKS) &&
                            !Files.isDirectory(path, LinkOption.NOFOLLOW_LINKS)) return false
                    }
                }
            }
            stack.isNotEmpty()
        } catch (_: IOException) { false } catch (_: IllegalArgumentException) { false }
    }

}
