package app.aemu.core

import java.io.File
import java.io.IOException
import java.nio.file.Files
import java.nio.file.LinkOption
import java.nio.file.Path
import java.util.ArrayDeque

/** Validate firmware-provided paths before fixup can create directories or its FRP image. */
internal object GuestWritePaths {
    fun requireSafe(root: File, directories: List<String>) {
        val base = root.toPath().toAbsolutePath().normalize()
        if (!Files.isDirectory(base, LinkOption.NOFOLLOW_LINKS)) throw IOException("unsafe firmware root")
        for (value in directories) {
            val path = value.trim()
            if (path.isEmpty() || path.startsWith('#')) continue
            resolve(base, path)
        }
        for (name in listOf("default.prop", "system/build.prop", "vendor/build.prop", "system/vendor/build.prop")) {
            val path = resolve(base, name)
            if (!Files.isRegularFile(path, LinkOption.NOFOLLOW_LINKS)) continue
            if (Files.size(path) > 8L * 1024 * 1024) throw IOException("firmware property file too large")
            val frp = Files.newInputStream(path, LinkOption.NOFOLLOW_LINKS).bufferedReader().useLines { lines ->
                lines.firstOrNull { it.startsWith("ro.frp.pst=") }?.substringAfter('=')?.trim()
            }
            if (frp != null) {
                if (frp.startsWith("/dev/")) resolve(base, frp)
                break
            }
        }
    }

    private fun resolve(base: Path, input: String): Path {
        if (input.length > 8192 || input.contains('\\') || input.contains("//") || input.indexOf('\u0000') >= 0) throw IOException("invalid guest write path")
        val components = input.split('/').filter { it.isNotEmpty() && it != "." }
        if (components.isEmpty() || components.any { it == ".." }) throw IOException("unsafe guest write path")
        val queue = ArrayDeque<String>(); components.forEach(queue::addLast)
        val stack = ArrayList<String>()
        var followed = 0
        while (queue.isNotEmpty()) {
            when (val part = queue.removeFirst()) {
                "", "." -> Unit
                ".." -> if (stack.isEmpty()) throw IOException("guest write path escapes root") else stack.removeAt(stack.lastIndex)
                else -> {
                    stack.add(part)
                    val path = stack.fold(base) { p, name -> p.resolve(name) }
                    if (Files.isSymbolicLink(path)) {
                        if (++followed > 256) throw IOException("guest write link cycle")
                        val target = Files.readSymbolicLink(path)
                        if (target.toString().contains('\\')) throw IOException("guest write link escapes root")
                        stack.removeAt(stack.lastIndex)
                        val parts = if (target.isAbsolute) {
                            val absolute = target.toAbsolutePath().normalize()
                            if (!absolute.startsWith(base)) throw IOException("guest write link escapes root")
                            stack.clear()
                            base.relativize(absolute).toString().replace(File.separatorChar, '/').split('/')
                        } else target.toString().replace(File.separatorChar, '/').split('/')
                        parts.asReversed().forEach(queue::addFirst)
                    } else if (queue.isNotEmpty() && Files.exists(path, LinkOption.NOFOLLOW_LINKS) &&
                        !Files.isDirectory(path, LinkOption.NOFOLLOW_LINKS)) throw IOException("guest write ancestor is not a directory")
                }
            }
        }
        if (stack.isEmpty()) throw IOException("guest write path points to root")
        return stack.fold(base) { p, name -> p.resolve(name) }
    }
}
