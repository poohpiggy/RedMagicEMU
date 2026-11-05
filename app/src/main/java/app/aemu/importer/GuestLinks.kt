package app.aemu.importer

import java.io.IOException
import java.util.ArrayDeque

/** Validate the complete link graph before creating any link in the private guest tree. */
internal object GuestLinks {
    private fun path(name: String): List<String> {
        if (name.length > 8192 || name.isEmpty() || name.startsWith('/') || name.contains('\\') || name.indexOf('\u0000') >= 0)
            throw IOException("Invalid guest link path")
        val p = name.split('/')
        if (p.any { it.isEmpty() || it == "." || it == ".." }) throw IOException("Invalid guest link path")
        return p
    }
    fun hostTarget(name: String, target: String): String {
        val p = path(name)
        if (target.length > 4096 || target.isEmpty() || target.contains('\\') || target.indexOf('\u0000') >= 0) throw IOException("Invalid guest link target")
        return if (target.startsWith('/')) "../".repeat(p.size - 1) + target.trimStart('/').ifEmpty { "." } else target
    }
    fun validate(links: Map<String, String>) {
        for ((name, target) in links) {
            val p = path(name)
            hostTarget(name, target)
            for (i in 1 until p.size) if (links.containsKey(p.take(i).joinToString("/")))
                throw IOException("Link occurs beneath a link")
        }
        for ((name, target) in links) {
            val parent = path(name).dropLast(1).toMutableList()
            val queue = ArrayDeque<String>()
            if (target.startsWith('/')) parent.clear()
            target.split('/').forEach { queue.addLast(it) }
            var traversed = 0
            while (queue.isNotEmpty()) {
                when (val part = queue.removeFirst()) {
                    "", "." -> Unit
                    ".." -> if (parent.isEmpty()) throw IOException("Guest link escapes root: $name") else parent.removeAt(parent.lastIndex)
                    else -> {
                        parent.add(part)
                        val nested = links[parent.joinToString("/")]
                        if (nested != null) {
                            if (++traversed > 256) throw IOException("Guest link cycle: $name")
                            parent.removeAt(parent.lastIndex)
                            if (nested.startsWith('/')) parent.clear()
                            nested.split('/').asReversed().forEach { queue.addFirst(it) }
                        }
                    }
                }
            }
        }
    }
}
