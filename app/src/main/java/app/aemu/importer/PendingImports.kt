package app.aemu.importer

import app.aemu.core.ImageId
import java.io.Closeable
import java.io.File
import java.io.IOException
import java.nio.channels.FileChannel
import java.nio.channels.FileLock
import java.nio.channels.OverlappingFileLockException
import java.nio.file.Files
import java.nio.file.LinkOption
import java.nio.file.Path
import java.nio.file.StandardOpenOption
import java.nio.file.StandardCopyOption
import java.util.UUID

/** Private in-progress imports. OS locks distinguish abandoned work from another live importer. */
internal object PendingImports {
    private const val DIRECTORY = ".firmware-pending"
    private const val MARKER = ".owner"
    private const val PUBLISH = ".publish"
    private const val CLOSING = ".closing"
    private const val INTERRUPTED = ".interrupted-notice"
    private val noticeToken = Regex("[0-9a-f]{32}")
    private const val RESERVATION = ".redmagic-import-pending"
    private const val MAGIC = "RedMagicEMU pending import v1\n"
    private val candidate = Regex("pending-[0-9a-f]{32}")

    private fun manager(images: File): Path {
        val base = images.toPath().toAbsolutePath().normalize()
        if (!Files.isDirectory(base, LinkOption.NOFOLLOW_LINKS)) throw IOException("Invalid images directory")
        val pending = base.resolve(DIRECTORY)
        if (!Files.exists(pending, LinkOption.NOFOLLOW_LINKS)) {
            try { Files.createDirectory(pending) } catch (_: java.nio.file.FileAlreadyExistsException) { }
        }
        if (!Files.isDirectory(pending, LinkOption.NOFOLLOW_LINKS)) throw IOException("Invalid pending directory")
        return pending
    }

    private fun <T> locked(images: File, action: (Path) -> T): T = synchronized(this) {
        val root = manager(images)
        FileChannel.open(root.resolve(".management.lock"), StandardOpenOption.CREATE, StandardOpenOption.WRITE, LinkOption.NOFOLLOW_LINKS).use { channel ->
            channel.lock().use { action(root) }
        }
    }

    internal fun <T> withImagesLock(images: File, action: () -> T): T = locked(images) { action() }

    fun begin(images: File): Lease = locked(images) { root ->
        val dir = root.resolve("pending-" + UUID.randomUUID().toString().replace("-", ""))
        Files.createDirectory(dir)
        var channel: FileChannel? = null
        var lock: FileLock? = null
        try {
            channel = FileChannel.open(dir.resolve(".lease"), StandardOpenOption.CREATE_NEW, StandardOpenOption.WRITE, LinkOption.NOFOLLOW_LINKS)
            lock = channel.lock()
            Files.write(dir.resolve(MARKER), MAGIC.toByteArray(Charsets.UTF_8), StandardOpenOption.CREATE_NEW, StandardOpenOption.WRITE, LinkOption.NOFOLLOW_LINKS)
            Lease(images, dir.toFile(), channel, lock)
        } catch (t: Throwable) {
            try { lock?.close() } catch (e: Throwable) { t.addSuppressed(e) }
            try { channel?.close() } catch (e: Throwable) { t.addSuppressed(e) }
            try { ImportCleanup.remove(dir.toFile()) } catch (e: Throwable) { t.addSuppressed(e) }
            throw t
        }
    }

    /** A reservation hides a partially populated instance; it never serves as firmware proof. */
    fun isPublishing(instance: File): Boolean =
        Files.exists(instance.toPath().resolve(RESERVATION), LinkOption.NOFOLLOW_LINKS)

    private fun publishIdentity(dir: Path, id: String): ByteArray = "$id\n${dir.fileName}\n".toByteArray(Charsets.US_ASCII)

    /** Undo only a reservation that carries this exact lease identity. Never delete an unmarked target. */
    private fun recoverReservation(images: File, dir: Path): Boolean {
        val state = dir.resolve(PUBLISH)
        if (!Files.exists(state, LinkOption.NOFOLLOW_LINKS)) return false
        if (!Files.isRegularFile(state, LinkOption.NOFOLLOW_LINKS) || Files.size(state) !in 1..128)
            throw IOException("Cannot verify pending publication")
        val text = String(Files.readAllBytes(state), Charsets.US_ASCII)
        val lines = text.split('\n')
        if (lines.size != 3 || lines[2].isNotEmpty() || !ImageId.valid(lines[0]) || lines[1] != dir.fileName.toString())
            throw IOException("Cannot verify pending publication")
        val target = images.toPath().toAbsolutePath().normalize().resolve(lines[0])
        if (!Files.exists(target, LinkOption.NOFOLLOW_LINKS)) return false
        if (!Files.isDirectory(target, LinkOption.NOFOLLOW_LINKS)) throw IOException("Cannot verify reserved instance")
        val marker = target.resolve(RESERVATION)
        if (Files.exists(marker, LinkOption.NOFOLLOW_LINKS)) {
            if (!Files.isRegularFile(marker, LinkOption.NOFOLLOW_LINKS) || Files.size(marker) != text.toByteArray(Charsets.US_ASCII).size.toLong() ||
                !Files.readAllBytes(marker).contentEquals(text.toByteArray(Charsets.US_ASCII)))
                throw IOException("Cannot verify reserved instance ownership")
            ImportCleanup.remove(target.toFile())
            return false
        }
        // A missing marker is either already published, or a crash before the marker was written.
        // Neither case authorizes deleting an unmarked directory.
        val children = Files.newDirectoryStream(target).use { ds -> ds.map { it.fileName.toString() }.toSet() }
        if (children.isEmpty()) return false
        if (!Files.isRegularFile(target.resolve("image.json"), LinkOption.NOFOLLOW_LINKS) ||
            !Files.isDirectory(target.resolve("root"), LinkOption.NOFOLLOW_LINKS))
            throw IOException("Cannot verify unmarked instance; preserving pending data")
        return true
    }

    private fun readNotice(root: Path): String? {
        val path = root.resolve(INTERRUPTED)
        if (!Files.exists(path, LinkOption.NOFOLLOW_LINKS)) return null
        if (!Files.isRegularFile(path, LinkOption.NOFOLLOW_LINKS) || Files.size(path) != 32L)
            throw IOException("Invalid interrupted import notice")
        return String(Files.readAllBytes(path), Charsets.US_ASCII).also {
            if (!noticeToken.matches(it)) throw IOException("Invalid interrupted import notice")
        }
    }

    private fun recordInterrupted(root: Path) {
        readNotice(root) // Reject unknown files before replacing the marker.
        val temp = Files.createTempFile(root, ".notice-", ".tmp")
        try {
            FileChannel.open(temp, StandardOpenOption.WRITE, LinkOption.NOFOLLOW_LINKS).use { out ->
                val data = java.nio.ByteBuffer.wrap(UUID.randomUUID().toString().replace("-", "").toByteArray(Charsets.US_ASCII))
                while (data.hasRemaining()) out.write(data)
                out.force(true)
            }
            Files.move(temp, root.resolve(INTERRUPTED), StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING)
        } finally { Files.deleteIfExists(temp) }
    }

    /** Keeps the notice until it is acknowledged, even if the process dies during cleanup. */
    fun cleanAbandoned(images: File): Boolean = locked(images) { root ->
        readNotice(root)
        Files.newDirectoryStream(root).use { entries ->
            for (dir in entries) {
                if (!candidate.matches(dir.fileName.toString()) || !Files.isDirectory(dir, LinkOption.NOFOLLOW_LINKS)) continue
                val owner = dir.resolve(MARKER)
                val lease = dir.resolve(".lease")
                val entries = Files.newDirectoryStream(dir).use { ds -> ds.map { it.fileName.toString() }.toSet() }
                // A crash between mkdir and marker creation leaves no user content.
                if (entries.isEmpty()) { Files.delete(dir); continue }
                val onlyEmptyLease = entries == setOf(".lease") && Files.isRegularFile(lease, LinkOption.NOFOLLOW_LINKS) && Files.size(lease) == 0L
                val magic = MAGIC.toByteArray(Charsets.UTF_8)
                val ownerSize = if (Files.isRegularFile(owner, LinkOption.NOFOLLOW_LINKS)) Files.size(owner) else -1L
                val ownerBytes = if (ownerSize in 0..magic.size.toLong()) Files.readAllBytes(owner) else null
                val validOwner = ownerBytes?.contentEquals(magic) == true
                // No payload can be written until the owner marker was written completely.
                val partialOwner = entries == setOf(".lease", MARKER) && ownerBytes != null && ownerBytes.size < magic.size &&
                    ownerBytes.contentEquals(magic.copyOf(ownerBytes.size))
                if (!onlyEmptyLease && !validOwner && !partialOwner) continue
                if (!Files.isRegularFile(lease, LinkOption.NOFOLLOW_LINKS)) continue
                var abandoned = false
                FileChannel.open(lease, StandardOpenOption.WRITE, LinkOption.NOFOLLOW_LINKS).use { channel ->
                    val held = try { channel.tryLock() } catch (_: OverlappingFileLockException) { null }
                    if (held != null) { held.close(); abandoned = true }
                }
                if (abandoned) {
                    val published = recoverReservation(images, dir)
                    if (validOwner && !published && CLOSING !in entries) recordInterrupted(root)
                    ImportCleanup.remove(dir.toFile())
                }
            }
        }
        readNotice(root) != null
    }

    fun interruptedNoticeToken(images: File): String? = locked(images) { root -> readNotice(root) }

    fun acknowledgeInterrupted(images: File, token: String): Boolean = locked(images) { root ->
        if (readNotice(root) != token) return@locked false
        Files.deleteIfExists(root.resolve(INTERRUPTED))
        true
    }

    class Lease internal constructor(private val images: File, val dir: File, private val channel: FileChannel, private val lock: FileLock) : Closeable {
        private var closed = false
        private var committed = false

        fun commit(id: String, publish: (() -> Unit) -> Unit = { it() }, onPhase: (String) -> Unit = {}): File = locked(images) {
            if (closed || committed) throw IOException("Import lease is closed")
            if (!ImageId.valid(id)) throw IOException("Invalid instance ID")
            val pending = dir.toPath().resolve(id)
            val dest = images.toPath().toAbsolutePath().normalize().resolve(id)
            if (!Files.isDirectory(pending, LinkOption.NOFOLLOW_LINKS) ||
                !Files.isDirectory(pending.resolve("root"), LinkOption.NOFOLLOW_LINKS)) throw IOException("Incomplete pending instance")
            val metadata = pending.resolve("image.json")
            if (!Files.isRegularFile(metadata, LinkOption.NOFOLLOW_LINKS) || Files.size(metadata) == 0L)
                throw IOException("Incomplete pending instance")
            // Check while holding the cross-process publication lock so two concurrent imports
            // cannot both publish. Preserve existing data, including metadata we cannot parse.
            val existing = Files.newDirectoryStream(images.toPath()).use { entries ->
                entries.any { entry -> ImageId.valid(entry.fileName.toString()) &&
                    (Files.exists(entry.resolve("image.json"), LinkOption.NOFOLLOW_LINKS) ||
                        Files.exists(entry.resolve(RESERVATION), LinkOption.NOFOLLOW_LINKS)) }
            }
            if (existing) throw IOException("An active instance already exists")
            if (Files.exists(dest, LinkOption.NOFOLLOW_LINKS)) throw IOException("Instance already exists")
            val contents = Files.newDirectoryStream(pending).use { ds -> ds.toList().sortedBy { it.fileName.toString() } }
            if (contents.any { it.fileName.toString() == RESERVATION }) throw IOException("Reserved instance filename")
            val identity = publishIdentity(dir.toPath(), id)
            val state = dir.toPath().resolve(PUBLISH)
            val stateTemp = Files.createTempFile(dir.toPath(), ".publish-", ".tmp")
            try {
                FileChannel.open(stateTemp, StandardOpenOption.WRITE, LinkOption.NOFOLLOW_LINKS).use { out ->
                    val bytes = java.nio.ByteBuffer.wrap(identity)
                    while (bytes.hasRemaining()) out.write(bytes)
                    out.force(true)
                }
                Files.move(stateTemp, state, java.nio.file.StandardCopyOption.ATOMIC_MOVE)
            } finally { Files.deleteIfExists(stateTemp) }
            try {
                // mkdir is the no-replace reservation; an existing instance is never renamed over.
                Files.createDirectory(dest)
            } catch (t: Throwable) {
                try { Files.deleteIfExists(state) } catch (cleanup: Throwable) { t.addSuppressed(cleanup) }
                throw t
            }
            onPhase("pre-marker")
            val marker = dest.resolve(RESERVATION)
            FileChannel.open(marker, StandardOpenOption.CREATE_NEW, StandardOpenOption.WRITE, LinkOption.NOFOLLOW_LINKS).use { out ->
                val bytes = java.nio.ByteBuffer.wrap(identity)
                while (bytes.hasRemaining()) out.write(bytes)
                out.force(true)
            }
            onPhase("reserved")
            for (item in contents) {
                val target = dest.resolve(item.fileName.toString())
                if (Files.exists(target, LinkOption.NOFOLLOW_LINKS)) throw IOException("Reserved instance changed during publication")
                Files.move(item, target, java.nio.file.StandardCopyOption.ATOMIC_MOVE)
            }
            onPhase("populated")
            // Readers hide this directory while the marker exists. Its removal publishes the complete instance.
            publish {
                Files.delete(marker)
                committed = true
            }
            onPhase("published")
            dest.toFile()
        }

        override fun close() {
            if (closed) return
            locked(images) {
                if (closed) return@locked
                var failure: Throwable? = null
                try {
                    Files.write(dir.toPath().resolve(CLOSING), byteArrayOf(), StandardOpenOption.CREATE_NEW, StandardOpenOption.WRITE, LinkOption.NOFOLLOW_LINKS)
                } catch (t: Throwable) { failure = t }
                try { if (!committed) recoverReservation(images, dir.toPath()) } catch (t: Throwable) { if (failure == null) failure = t else failure.addSuppressed(t) }
                try { lock.close() } catch (t: Throwable) { if (failure == null) failure = t else failure.addSuppressed(t) }
                try { channel.close() } catch (t: Throwable) { if (failure == null) failure = t else failure.addSuppressed(t) }
                closed = true
                if (failure != null) throw IOException("Cannot clean pending publication", failure)
                ImportCleanup.remove(dir)
            }
        }
    }
}
