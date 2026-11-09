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
import java.nio.file.NoSuchFileException
import java.nio.file.attribute.BasicFileAttributes
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
    internal const val GUEST_QUARANTINE = ".guest-init-quarantine"
    private val GUEST_QUARANTINE_MAGIC = "RedMagicEMU guest initialization quarantine v1\n".toByteArray(Charsets.US_ASCII)
    /** Faults always throw. They cannot replace or bypass the production sync operations. */
    internal enum class QuarantineFault { BEFORE_CREATE, AFTER_CREATE, AFTER_PARTIAL_WRITE, BEFORE_FILE_SYNC, BEFORE_DIRECTORY_SYNC, AFTER_DIRECTORY_SYNC }
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

    /** Only ENOENT proves absence. Links, directories, malformed files and unknown types all quarantine. */
    private fun quarantinePresent(dir: Path): Boolean = try {
        Files.readAttributes(dir.resolve(GUEST_QUARANTINE), BasicFileAttributes::class.java, LinkOption.NOFOLLOW_LINKS)
        true
    } catch (_: NoSuchFileException) { false }

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
                // The original importer lease does not cover a potentially surviving Guest writer.
                // Never infer that it stopped merely because the importer process lost its lock.
                if (quarantinePresent(dir)) {
                    val lease = dir.resolve(".lease")
                    if (Files.isRegularFile(lease, LinkOption.NOFOLLOW_LINKS)) {
                        FileChannel.open(lease, StandardOpenOption.WRITE, LinkOption.NOFOLLOW_LINKS).use { channel ->
                            val available = try { channel.tryLock() } catch (_: OverlappingFileLockException) { null }
                            if (available != null) available.use {
                                // Preserve an existing token so that an acknowledgement in flight remains valid.
                                if (readNotice(root) == null) recordInterrupted(root)
                            }
                        }
                    }
                    continue
                }
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
        private var preserveOnClose = false
        private var quarantineKey: Any? = null
        // Capture before exposing the lease. A platform without stable file keys cannot enter quarantine mode.
        private val imageKey = fileKey(images.toPath())
        private val managerKey = fileKey(dir.parentFile.toPath())
        private val pendingKey = fileKey(dir.toPath())
        private val leaseKey = fileKey(dir.toPath().resolve(".lease"))

        private fun fileKey(path: Path): Any? = try {
            Files.readAttributes(path, BasicFileAttributes::class.java, LinkOption.NOFOLLOW_LINKS).fileKey()
        } catch (_: Exception) { null }

        /** Checks cooperating owners and the names captured at lease creation; not a process-tree proof. */
        private fun requireLiveQuarantineLease() {
            if (closed || committed || !channel.isOpen || !lock.isValid || !candidate.matches(dir.name))
                throw IOException("Import lease is not live")
            val checks = listOf(images.toPath() to imageKey, dir.parentFile.toPath() to managerKey,
                dir.toPath() to pendingKey, dir.toPath().resolve(".lease") to leaseKey)
            for ((path, expected) in checks) {
                if (expected == null || Files.isSymbolicLink(path) || fileKey(path) != expected)
                    throw IOException("Import lease identity changed")
            }
            val owner = dir.toPath().resolve(MARKER)
            if (!Files.isRegularFile(owner, LinkOption.NOFOLLOW_LINKS) ||
                Files.size(owner) != MAGIC.toByteArray(Charsets.UTF_8).size.toLong() ||
                !Files.readAllBytes(owner).contentEquals(MAGIC.toByteArray(Charsets.UTF_8)))
                throw IOException("Import ownership cannot be verified")
        }

        /**
         * Persist quarantine before creating any Guest writer. A thrown exception never grants permission
         * to start one. Once attempted, this lease will not clean up its tree in this process, including
         * if marker creation itself failed. There is deliberately no clear/publish method here.
         */
        internal fun markGuestQuarantine() = markGuestQuarantineInternal(null)

        /** Each injected fault throws; tests cannot use this to replace a sync with a successful no-op. */
        internal fun markGuestQuarantineFailingAtForTests(fault: QuarantineFault): Nothing {
            markGuestQuarantineInternal(fault)
            throw IOException("Fault was not reached")
        }

        private fun markGuestQuarantineInternal(fault: QuarantineFault?) = locked(images) {
            if (closed || committed) throw IOException("Import lease is closed")
            // Set before any potentially failing validation or write, so close() never deletes the tree.
            preserveOnClose = true
            fun failAt(point: QuarantineFault) { if (fault == point) throw IOException("Injected quarantine failure") }
            requireLiveQuarantineLease()
            if (Files.exists(dir.toPath().resolve(PUBLISH), LinkOption.NOFOLLOW_LINKS))
                throw IOException("Cannot quarantine an existing publication")
            if (quarantinePresent(dir.toPath())) throw IOException("Import already quarantined")
            failAt(QuarantineFault.BEFORE_CREATE)
            val path = dir.toPath().resolve(GUEST_QUARANTINE)
            FileChannel.open(path, StandardOpenOption.CREATE_NEW, StandardOpenOption.WRITE, LinkOption.NOFOLLOW_LINKS).use { out ->
                failAt(QuarantineFault.AFTER_CREATE)
                val bytes = java.nio.ByteBuffer.wrap(GUEST_QUARANTINE_MAGIC)
                val initialLimit = bytes.limit()
                bytes.limit(initialLimit / 2)
                while (bytes.hasRemaining()) out.write(bytes)
                failAt(QuarantineFault.AFTER_PARTIAL_WRITE)
                bytes.limit(initialLimit)
                while (bytes.hasRemaining()) out.write(bytes)
                failAt(QuarantineFault.BEFORE_FILE_SYNC)
                out.force(true)
            }
            failAt(QuarantineFault.BEFORE_DIRECTORY_SYNC)
            FileChannel.open(dir.toPath(), StandardOpenOption.READ, LinkOption.NOFOLLOW_LINKS).use { it.force(true) }
            failAt(QuarantineFault.AFTER_DIRECTORY_SYNC)
            requireLiveQuarantineLease()
            if (!Files.isRegularFile(path, LinkOption.NOFOLLOW_LINKS) ||
                Files.size(path) != GUEST_QUARANTINE_MAGIC.size.toLong() ||
                !Files.readAllBytes(path).contentEquals(GUEST_QUARANTINE_MAGIC))
                throw IOException("Quarantine cannot be verified")
            quarantineKey = fileKey(path) ?: throw IOException("Quarantine identity cannot be verified")
        }

        /** Builds a reserved short-path instance only; this does not authorize starting a Guest. */
        internal fun prepareHidden(id: String, onPhase: (String) -> Unit = {}): File = locked(images) {
            requireLiveQuarantineLease()
            val q = dir.toPath().resolve(GUEST_QUARANTINE)
            fun checkQuarantine() {
                if (quarantineKey == null || fileKey(q) != quarantineKey ||
                    !Files.isRegularFile(q, LinkOption.NOFOLLOW_LINKS) ||
                    Files.size(q) != GUEST_QUARANTINE_MAGIC.size.toLong() ||
                    !Files.readAllBytes(q).contentEquals(GUEST_QUARANTINE_MAGIC))
                    throw IOException("Durable quarantine is not verified")
            }
            checkQuarantine()
            if (!ImageId.valid(id)) throw IOException("Invalid instance ID")
            val pending = dir.toPath().resolve(id)
            val metadata = pending.resolve("image.json")
            if (!Files.isDirectory(pending, LinkOption.NOFOLLOW_LINKS) ||
                !Files.isDirectory(pending.resolve("root"), LinkOption.NOFOLLOW_LINKS) ||
                !Files.isRegularFile(metadata, LinkOption.NOFOLLOW_LINKS) || Files.size(metadata) == 0L)
                throw IOException("Incomplete pending instance")
            val pendingKey = fileKey(pending) ?: throw IOException("Pending instance identity cannot be verified")
            val contents = Files.newDirectoryStream(pending).use { ds -> ds.toList().sortedBy { it.fileName.toString() } }
            if (contents.any { it.fileName.toString() == RESERVATION }) throw IOException("Reserved instance filename")
            val root = images.toPath().toAbsolutePath().normalize()
            val dest = root.resolve(id)
            val existing = Files.newDirectoryStream(root).use { entries ->
                entries.any { entry -> ImageId.valid(entry.fileName.toString()) &&
                    (Files.exists(entry.resolve("image.json"), LinkOption.NOFOLLOW_LINKS) ||
                        Files.exists(entry.resolve(RESERVATION), LinkOption.NOFOLLOW_LINKS)) }
            }
            if (existing || Files.exists(dest, LinkOption.NOFOLLOW_LINKS)) throw IOException("Instance already exists")
            val identity = publishIdentity(dir.toPath(), id)
            fun writeNew(path: Path) {
                FileChannel.open(path, StandardOpenOption.CREATE_NEW, StandardOpenOption.WRITE, LinkOption.NOFOLLOW_LINKS).use { out ->
                    val bytes = java.nio.ByteBuffer.wrap(identity)
                    while (bytes.hasRemaining()) out.write(bytes)
                    out.force(true)
                }
            }
            fun syncDirectory(path: Path) {
                FileChannel.open(path, StandardOpenOption.READ, LinkOption.NOFOLLOW_LINKS).use { it.force(true) }
            }
            val state = dir.toPath().resolve(PUBLISH)
            writeNew(state)
            syncDirectory(dir.toPath())
            val stateKey = fileKey(state) ?: throw IOException("Pending publication identity cannot be verified")
            Files.createDirectory(dest) // mkdir cannot replace an existing directory, file, or link.
            val targetKey = fileKey(dest) ?: throw IOException("Reservation identity cannot be verified")
            onPhase("created")
            val marker = dest.resolve(RESERVATION)
            writeNew(marker)
            syncDirectory(dest)
            syncDirectory(root)
            val markerKey = fileKey(marker) ?: throw IOException("Reservation marker identity cannot be verified")
            fun checkReservation() {
                requireLiveQuarantineLease()
                checkQuarantine()
                if (fileKey(pending) != pendingKey || !Files.isDirectory(pending, LinkOption.NOFOLLOW_LINKS) ||
                    fileKey(state) != stateKey || !Files.isRegularFile(state, LinkOption.NOFOLLOW_LINKS) ||
                    Files.size(state) != identity.size.toLong() || !Files.readAllBytes(state).contentEquals(identity) ||
                    fileKey(dest) != targetKey || !Files.isDirectory(dest, LinkOption.NOFOLLOW_LINKS) ||
                    fileKey(marker) != markerKey || !Files.isRegularFile(marker, LinkOption.NOFOLLOW_LINKS) ||
                    Files.size(marker) != identity.size.toLong() || !Files.readAllBytes(marker).contentEquals(identity))
                    throw IOException("Reservation identity changed")
            }
            checkReservation()
            onPhase("reserved")
            for (item in contents) {
                checkReservation()
                Files.move(item, dest.resolve(item.fileName.toString()))
            }
            syncDirectory(dest)
            syncDirectory(pending)
            onPhase("populated")
            checkReservation()
            dest.toFile()
        }

        fun commit(id: String, publish: (() -> Unit) -> Unit = { it() }, onPhase: (String) -> Unit = {}): File = locked(images) {
            if (closed || committed) throw IOException("Import lease is closed")
            if (preserveOnClose || quarantinePresent(dir.toPath())) throw IOException("Guest initialization is quarantined")
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
                if (preserveOnClose || quarantinePresent(dir.toPath())) throw IOException("Guest initialization is quarantined")
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
                // Any marker, including malformed or symlink, forbids both recovery and removal.
                // An I/O error when checking is also treated as unknown and thus preserved.
                val quarantine = preserveOnClose || try { quarantinePresent(dir.toPath()) } catch (_: IOException) { true }
                if (quarantine) {
                    var failure: Throwable? = null
                    try { lock.close() } catch (t: Throwable) { failure = t }
                    try { channel.close() } catch (t: Throwable) { if (failure == null) failure = t else failure.addSuppressed(t) }
                    closed = true
                    if (failure != null) throw IOException("Cannot close quarantined import lease", failure)
                    return@locked
                }
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
