package app.aemu.core

import java.io.File
import java.io.IOException
import java.nio.file.Files
import java.nio.file.LinkOption
import java.nio.file.Path
import java.nio.file.Paths
import java.nio.file.StandardCopyOption

/** Applies the signed deterministic partition link rebase; on ordinary failure restores the original tree. */
internal object PartitionLinkMove {
    fun move(root: File, name: String, beforeMove: () -> Unit = {}, afterMove: () -> Unit = {}): Boolean {
        if (name !in setOf("product", "system_ext")) throw IOException("invalid partition")
        val base = root.toPath().toAbsolutePath().normalize()
        val source = base.resolve("system").resolve(name)
        val destination = base.resolve(name)
        if (!Files.isDirectory(source, LinkOption.NOFOLLOW_LINKS)) return false
        if (Files.exists(destination, LinkOption.NOFOLLOW_LINKS)) {
            if (!Files.isSymbolicLink(destination) && (!Files.isDirectory(destination, LinkOption.NOFOLLOW_LINKS) ||
                Files.newDirectoryStream(destination).use { it.iterator().hasNext() })) return false
        }
        val rules = PartitionLinkPlan.plan(root, name)
        val backup = base.resolve(".rootlinks-$name-txn")
        Files.createDirectory(backup)
        val oldDestination = backup.resolve("placeholder")
        val applied = mutableListOf<PartitionLinkPlan.Rule>()
        var savedDestination = false
        var moved = false
        var sourceLink = false
        try {
            if (Files.exists(destination, LinkOption.NOFOLLOW_LINKS)) {
                Files.move(destination, oldDestination, StandardCopyOption.ATOMIC_MOVE)
                savedDestination = true
            }
            for (rule in rules) {
                replace(base.resolve(rule.oldPath), rule.originalTarget, rule.updatedTarget, backup)
                applied += rule
            }
            beforeMove()
            Files.move(source, destination, StandardCopyOption.ATOMIC_MOVE)
            moved = true
            afterMove()
            Files.createSymbolicLink(source, Paths.get("../$name"))
            sourceLink = true
        } catch (failure: Throwable) {
            var rollbackFailed = false
            fun rollback(action: () -> Unit) {
                try { action() } catch (e: Throwable) { failure.addSuppressed(e); rollbackFailed = true }
            }
            if (sourceLink) rollback { Files.delete(source) }
            if (moved) rollback { Files.move(destination, source, StandardCopyOption.ATOMIC_MOVE) }
            for (rule in applied.asReversed()) rollback { replace(base.resolve(rule.oldPath), rule.updatedTarget, rule.originalTarget, backup) }
            if (savedDestination && Files.exists(oldDestination, LinkOption.NOFOLLOW_LINKS))
                rollback { Files.move(oldDestination, destination, StandardCopyOption.ATOMIC_MOVE) }
            rollback { Files.delete(backup) }
            if (rollbackFailed) throw IOException("Partition move failed and rollback is incomplete", failure)
            throw failure
        }
        try {
            if (savedDestination) Files.delete(oldDestination)
            Files.delete(backup)
        } catch (e: IOException) { throw IOException("Partition moved but temporary backup cleanup failed", e) }
        return true
    }

    /** Call only with rules from an authenticated baseline; never trust a transaction marker alone. */
    fun recover(root: File, partition: PartitionCompatibility.Rule, links: List<PartitionLinkPlan.Rule>): Boolean {
        val name = partition.name
        if (name !in setOf("product", "system_ext")) throw IOException("invalid partition recovery")
        val base = root.toPath().toAbsolutePath().normalize()
        if (!Files.isDirectory(base, LinkOption.NOFOLLOW_LINKS) || !Files.isDirectory(base.resolve("system"), LinkOption.NOFOLLOW_LINKS))
            throw IOException("unsafe partition recovery root")
        val txn = base.resolve(".rootlinks-$name-txn")
        if (!Files.exists(txn, LinkOption.NOFOLLOW_LINKS)) return false
        if (!Files.isDirectory(txn, LinkOption.NOFOLLOW_LINKS)) throw IOException("unsafe partition transaction")
        val names = Files.newDirectoryStream(txn).use { ds -> ds.map { it.fileName.toString() }.toSet() }
        if (names.any { it !in setOf("placeholder", "link") }) throw IOException("unknown partition transaction data")
        if (links.map { it.oldPath }.distinct().size != links.size) throw IOException("invalid signed partition links")
        for (rule in links) {
            if (!rule.oldPath.startsWith("system/$name/") || rule.newPath != rule.oldPath.removePrefix("system/") ||
                rule.oldPath.split('/').any { it.isEmpty() || it == "." || it == ".." || it.contains('\\') })
                throw IOException("invalid signed partition link")
        }
        val source = base.resolve("system").resolve(name)
        val destination = base.resolve(name)
        val placeholder = txn.resolve("placeholder")
        val temp = txn.resolve("link")
        val hasPlaceholder = Files.exists(placeholder, LinkOption.NOFOLLOW_LINKS)
        if (hasPlaceholder && !PartitionCompatibility.matchesOriginal(placeholder, partition.originalTarget))
            throw IOException("partition transaction placeholder differs from signed baseline")
        if (Files.exists(temp, LinkOption.NOFOLLOW_LINKS)) {
            if (!Files.isSymbolicLink(temp)) throw IOException("invalid partition transaction link")
            val target = Files.readSymbolicLink(temp).toString().replace(File.separatorChar, '/')
            if (links.none { it.originalTarget == target || it.updatedTarget == target }) throw IOException("unknown partition transaction link")
        }
        val sourceIsDirectory = Files.isDirectory(source, LinkOption.NOFOLLOW_LINKS)
        val sourceIsLink = Files.isSymbolicLink(source)
        val sourceMissing = !Files.exists(source, LinkOption.NOFOLLOW_LINKS)
        if (sourceIsLink && Files.readSymbolicLink(source).toString().replace(File.separatorChar, '/') != "../$name")
            throw IOException("unexpected partition source link")
        val destinationIsDirectory = Files.isDirectory(destination, LinkOption.NOFOLLOW_LINKS)
        val moved = (sourceIsLink || sourceMissing) && destinationIsDirectory
        if (!sourceIsDirectory && !moved) throw IOException("unrecognized partition transaction state")
        val directory = if (moved) destination else source
        for (rule in links) {
            val relative = rule.oldPath.removePrefix("system/$name/")
            var path = directory
            val pieces = relative.split('/')
            for ((i, piece) in pieces.withIndex()) {
                path = path.resolve(piece)
                if (i < pieces.lastIndex && !Files.isDirectory(path, LinkOption.NOFOLLOW_LINKS)) throw IOException("unsafe partition recovery path")
            }
            if (!Files.isSymbolicLink(path)) throw IOException("missing partition recovery link")
            val target = Files.readSymbolicLink(path).toString().replace(File.separatorChar, '/')
            if (target != rule.originalTarget && target != rule.updatedTarget) throw IOException("unexpected partition recovery link")
        }
        val destinationOriginal = PartitionCompatibility.matchesOriginal(destination, partition.originalTarget)
        val originalWasAbsent = PartitionCompatibility.matchesOriginal(txn.resolve("absent"), partition.originalTarget)
        if (!moved && !(destinationOriginal && !hasPlaceholder || !Files.exists(destination, LinkOption.NOFOLLOW_LINKS) && (hasPlaceholder || originalWasAbsent)))
            throw IOException("unrecognized partition placeholder state")
        if (moved && !hasPlaceholder && !originalWasAbsent) {
            // Placeholder was already removed after the fully completed move: only finalize a complete state.
            if (!sourceIsLink || links.any { rule ->
                val path = base.resolve(rule.newPath)
                !Files.isSymbolicLink(path) || Files.readSymbolicLink(path).toString().replace(File.separatorChar, '/') != rule.updatedTarget
            }) throw IOException("partition transaction lost its placeholder")
            Files.deleteIfExists(temp)
            Files.delete(txn)
            return true
        }
        Files.deleteIfExists(temp)
        if (sourceIsLink) Files.delete(source)
        if (moved) Files.move(destination, source, StandardCopyOption.ATOMIC_MOVE)
        for (rule in links.asReversed()) {
            val path = base.resolve(rule.oldPath)
            val target = Files.readSymbolicLink(path).toString().replace(File.separatorChar, '/')
            if (target == rule.updatedTarget) replace(path, rule.updatedTarget, rule.originalTarget, txn)
        }
        if (hasPlaceholder) Files.move(placeholder, destination, StandardCopyOption.ATOMIC_MOVE)
        Files.delete(txn)
        return true
    }

    private fun replace(path: Path, expected: String, replacement: String, backup: Path) {
        if (!Files.isSymbolicLink(path) || Files.readSymbolicLink(path).toString().replace(File.separatorChar, '/') != expected)
            throw IOException("Partition link changed before relocation: $path")
        val temp = backup.resolve("link")
        try {
            Files.createSymbolicLink(temp, Paths.get(replacement))
            Files.move(temp, path, StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING)
        } finally { Files.deleteIfExists(temp) }
    }
}
