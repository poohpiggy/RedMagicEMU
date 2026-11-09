package app.aemu.core

import java.io.Closeable
import java.io.File
import java.io.IOException
import java.nio.ByteBuffer
import java.nio.channels.FileChannel
import java.nio.file.FileAlreadyExistsException
import java.nio.file.Files
import java.nio.file.LinkOption.NOFOLLOW_LINKS
import java.nio.file.NoSuchFileException
import java.nio.file.OpenOption
import java.nio.file.Path
import java.nio.file.Paths
import java.nio.file.SecureDirectoryStream
import java.nio.file.StandardOpenOption.CREATE_NEW
import java.nio.file.StandardOpenOption.READ
import java.nio.file.StandardOpenOption.WRITE
import java.nio.file.attribute.BasicFileAttributeView
import java.nio.file.attribute.BasicFileAttributes
import java.util.UUID

/**
 * Persistent, single-attempt intent for a freshly imported instance.
 *
 * This store does not prove an instance is new, stop Guest writers, gate input,
 * or perform initialization. Callers must establish those facts separately.
 * A claim permits work only after the claim file and its directory are synced.
 * In-progress or malformed state is never treated as a fresh instance.
 */
internal class FirstRingtoneInitStore internal constructor(
    private val id: String,
    private val io: Io,
) : Closeable {
    internal interface Io : Closeable {
        fun verifyIdentity()
        fun exists(name: String): Boolean
        fun readAndSync(name: String): ByteArray
        fun createAndSync(name: String, bytes: ByteArray)
        fun syncDirectory()
    }

    enum class Status { UNTRACKED, FRESH, IN_PROGRESS, COMPLETE, INVALID }
    enum class RetryClaim { BASELINED, ALLOWED, ALREADY_ATTEMPTED, NOT_IN_PROGRESS }
    enum class RetryPreparation { BASELINED, ELIGIBLE, ALREADY_ATTEMPTED, NOT_IN_PROGRESS }
    class Claim internal constructor(internal val token: String)

    init { require(ImageId.valid(id)) { "Invalid instance ID" } }

    /** Only call while creating a proven-new instance, before its publication. */
    fun createFresh() {
        io.verifyIdentity()
        if (listOf(FRESH_FILE, CLAIM_FILE, DONE_FILE).any(io::exists)) throw IOException("State already exists")
        io.createAndSync(FRESH_FILE, freshBytes())
        io.syncDirectory()
        io.verifyIdentity()
    }

    /** Re-sync observations before authorizing an operation or accepting completion. */
    fun status(): Status {
        io.verifyIdentity()
        val result = statusChecked()
        io.verifyIdentity()
        return result
    }

    private fun statusChecked(readRecord: (String) -> ByteArray? = ::read): Status {
        val fresh = io.exists(FRESH_FILE)
        val claimed = io.exists(CLAIM_FILE)
        val done = io.exists(DONE_FILE)
        if (!fresh && !claimed && !done) return Status.UNTRACKED
        if (!fresh || readRecord(FRESH_FILE)?.contentEquals(freshBytes()) != true) return Status.INVALID
        if (!claimed && !done) { io.syncDirectory(); return Status.FRESH }
        if (!claimed) return Status.INVALID
        val token = tokenFrom(readRecord(CLAIM_FILE), "claim") ?: return Status.INVALID
        if (!done) { io.syncDirectory(); return Status.IN_PROGRESS }
        if (tokenFrom(readRecord(DONE_FILE), "done") != token) return Status.INVALID
        io.syncDirectory()
        return Status.COMPLETE
    }

    /** Returns the sole durable claim, or null when this instance must not start. */
    fun claim(): Claim? {
        io.verifyIdentity()
        if (status() != Status.FRESH) return null
        val token = UUID.randomUUID().toString().replace("-", "")
        try {
            io.createAndSync(CLAIM_FILE, record("claim", token))
        } catch (_: FileAlreadyExistsException) {
            return null
        }
        io.syncDirectory()
        io.verifyIdentity()
        if (status() != Status.IN_PROGRESS || tokenFrom(read(CLAIM_FILE), "claim") != token)
            throw IOException("Claim cannot be verified")
        io.verifyIdentity()
        return Claim(token)
    }

    /** Records observed default or user-selected values; does not prove provider quiescence. */
    fun complete(claim: Claim) {
        io.verifyIdentity()
        if (status() != Status.IN_PROGRESS || tokenFrom(read(CLAIM_FILE), "claim") != claim.token)
            throw IOException("Claim is not current")
        io.createAndSync(DONE_FILE, record("done", claim.token))
        io.syncDirectory()
        io.verifyIdentity()
        if (status() != Status.COMPLETE) throw IOException("Completion cannot be verified")
        io.verifyIdentity()
    }

    /** Record independently observed defaults for an existing claim; never authorizes a scan. */
    fun completeObservedIfInProgress(): Boolean {
        if (strictStatus() != Status.IN_PROGRESS) return false
        val token = tokenFrom(io.readAndSync(CLAIM_FILE), "claim") ?: throw IOException("Invalid claim")
        try {
            io.createAndSync(DONE_FILE, record("done", token))
        } catch (e: FileAlreadyExistsException) {
            // A simultaneous verifier may have completed the same claim. Never accept a different token.
            if (strictStatus() != Status.COMPLETE || tokenFrom(io.readAndSync(CLAIM_FILE), "claim") != token)
                throw IOException("Concurrent completion cannot be verified", e)
            io.verifyIdentity()
            return true
        }
        io.syncDirectory()
        io.verifyIdentity()
        if (strictStatus() != Status.COMPLETE || tokenFrom(io.readAndSync(CLAIM_FILE), "claim") != token)
            throw IOException("Completion cannot be verified")
        io.verifyIdentity()
        return true
    }

    /** Persist the host boot on which a newly claimed scan is first launched. */
    fun recordInitialBoot(claim: Claim, bootId: String) {
        requireBootId(bootId)
        if (strictStatus() != Status.IN_PROGRESS || tokenFrom(io.readAndSync(CLAIM_FILE), "claim") != claim.token)
            throw IOException("Claim is not current")
        val bytes = retryRecord("baseline", claim.token, bootId)
        try { io.createAndSync(RETRY_BASELINE_FILE, bytes) }
        catch (e: FileAlreadyExistsException) {
            // A concurrent observer may have durably based this same claim on this boot.
            if (!io.readAndSync(RETRY_BASELINE_FILE).contentEquals(bytes))
                throw IOException("Conflicting initial boot record", e)
        }
        io.syncDirectory()
        io.verifyIdentity()
        if (strictStatus() != Status.IN_PROGRESS || tokenFrom(io.readAndSync(CLAIM_FILE), "claim") != claim.token ||
            !io.readAndSync(RETRY_BASELINE_FILE).contentEquals(bytes)) throw IOException("Initial boot record cannot be verified")
        io.verifyIdentity()
    }

    /** Check eligibility without consuming the boot's recovery attempt. */
    fun prepareUncertainRetry(bootId: String): RetryPreparation {
        requireBootId(bootId)
        if (strictStatus() != Status.IN_PROGRESS) return RetryPreparation.NOT_IN_PROGRESS
        val token = tokenFrom(io.readAndSync(CLAIM_FILE), "claim") ?: throw IOException("Invalid claim")
        if (!io.exists(RETRY_BASELINE_FILE)) {
            val bytes = retryRecord("baseline", token, bootId)
            try { io.createAndSync(RETRY_BASELINE_FILE, bytes) }
            catch (_: FileAlreadyExistsException) { return RetryPreparation.ALREADY_ATTEMPTED }
            io.syncDirectory()
            verifyRetryState(token, bytes)
            return RetryPreparation.BASELINED
        }
        val baseline = io.readAndSync(RETRY_BASELINE_FILE)
        val initialBoot = retryBootFrom(baseline, "baseline", token) ?: throw IOException("Invalid retry baseline")
        io.syncDirectory()
        io.verifyIdentity()
        if (bootId == initialBoot || io.exists(retryFile(bootId))) return RetryPreparation.ALREADY_ATTEMPTED
        verifyRetryState(token, baseline)
        return RetryPreparation.ELIGIBLE
    }

    /**
     * Allow at most one recovery attempt per host boot. Legacy in-progress
     * claims without a boot record are baselined and never retried immediately.
     * This does not establish that earlier provider work has stopped.
     */
    fun claimUncertainRetry(bootId: String): RetryClaim {
        when (prepareUncertainRetry(bootId)) {
            RetryPreparation.BASELINED -> return RetryClaim.BASELINED
            RetryPreparation.ALREADY_ATTEMPTED -> return RetryClaim.ALREADY_ATTEMPTED
            RetryPreparation.NOT_IN_PROGRESS -> return RetryClaim.NOT_IN_PROGRESS
            RetryPreparation.ELIGIBLE -> Unit
        }
        if (strictStatus() != Status.IN_PROGRESS) return RetryClaim.NOT_IN_PROGRESS
        val token = tokenFrom(io.readAndSync(CLAIM_FILE), "claim") ?: throw IOException("Invalid claim")
        val baseline = io.readAndSync(RETRY_BASELINE_FILE)
        val initialBoot = retryBootFrom(baseline, "baseline", token) ?: throw IOException("Invalid retry baseline")
        if (bootId == initialBoot) return RetryClaim.ALREADY_ATTEMPTED
        val retryFile = retryFile(bootId)
        val retry = retryRecord("retry", token, bootId)
        try { io.createAndSync(retryFile, retry) }
        catch (_: FileAlreadyExistsException) { return RetryClaim.ALREADY_ATTEMPTED }
        io.syncDirectory()
        verifyRetryState(token, baseline)
        if (!io.readAndSync(retryFile).contentEquals(retry)) throw IOException("Retry record cannot be verified")
        io.verifyIdentity()
        return RetryClaim.ALLOWED
    }

    private fun verifyRetryState(token: String, baseline: ByteArray) {
        io.verifyIdentity()
        if (strictStatus() != Status.IN_PROGRESS || tokenFrom(io.readAndSync(CLAIM_FILE), "claim") != token ||
            !io.readAndSync(RETRY_BASELINE_FILE).contentEquals(baseline)) throw IOException("Retry state changed")
        io.verifyIdentity()
    }

    private fun requireBootId(bootId: String) {
        require(BOOT_ID.matches(bootId) && UUID.fromString(bootId).toString() == bootId) { "Invalid boot ID" }
    }
    private fun retryRecord(kind: String, token: String, bootId: String) =
        "redmagic-ringtone-retry-v1\n$kind\n$id\n$token\n$bootId\n".toByteArray(Charsets.US_ASCII)
    private fun retryBootFrom(bytes: ByteArray, kind: String, token: String): String? {
        val text = String(bytes, Charsets.US_ASCII)
        val prefix = "redmagic-ringtone-retry-v1\n$kind\n$id\n$token\n"
        if (!text.startsWith(prefix) || !text.endsWith('\n')) return null
        val boot = text.substring(prefix.length, text.length - 1)
        return boot.takeIf { BOOT_ID.matches(it) && retryRecord(kind, token, it).contentEquals(bytes) }
    }
    private fun retryFile(bootId: String) = "$RETRY_FILE_PREFIX$bootId"

    private fun strictStatus(): Status {
        io.verifyIdentity()
        val result = statusChecked { io.readAndSync(it) }
        io.verifyIdentity()
        return result
    }

    private fun read(name: String): ByteArray? = try { io.readAndSync(name) } catch (_: IOException) { null }
    private fun freshBytes() = "redmagic-ringtone-v1\nfresh\n$id\n".toByteArray(Charsets.US_ASCII)
    private fun record(kind: String, token: String) =
        "redmagic-ringtone-v1\n$kind\n$id\n$token\n".toByteArray(Charsets.US_ASCII)
    private fun tokenFrom(bytes: ByteArray?, kind: String): String? {
        if (bytes == null) return null
        val text = String(bytes, Charsets.US_ASCII)
        val prefix = "redmagic-ringtone-v1\n$kind\n$id\n"
        if (!text.startsWith(prefix) || !text.endsWith('\n')) return null
        val token = text.substring(prefix.length, text.length - 1)
        return token.takeIf { it.matches(Regex("[0-9a-f]{32}")) && record(kind, it).contentEquals(bytes) }
    }

    override fun close() = io.close()

    companion object {
        const val FRESH_FILE = ".rme-ringtone-fresh"
        const val CLAIM_FILE = ".rme-ringtone-claim"
        const val DONE_FILE = ".rme-ringtone-done"
        const val RETRY_BASELINE_FILE = ".rme-ringtone-retry-baseline"
        const val RETRY_FILE_PREFIX = ".rme-ringtone-retry-boot-"
        private val BOOT_ID = Regex("[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}")

        /** Fails closed if anchored directory operations or directory fsync are unavailable. */
        fun open(instance: File, id: String): FirstRingtoneInitStore {
            if (!ImageId.valid(id) || instance.name != id) throw IOException("Invalid instance identity")
            return FirstRingtoneInitStore(id, SecureIo.open(instance.toPath()))
        }
    }

    /** All state-file access is relative to an opened, identity-checked directory. */
    private class SecureIo(
        private val dir: SecureDirectoryStream<Path>,
        private val path: Path,
        private val identity: Any,
    ) : Io {
        override fun verifyIdentity() {
            val opened = dir.getFileAttributeView(BasicFileAttributeView::class.java).readAttributes().fileKey()
            if (opened != identity || checkedIdentity(path) != identity) throw IOException("Directory identity changed")
        }

        override fun exists(name: String): Boolean = try {
            dir.getFileAttributeView(Paths.get(name), BasicFileAttributeView::class.java, NOFOLLOW_LINKS).readAttributes()
            true
        } catch (_: NoSuchFileException) { false }

        override fun readAndSync(name: String): ByteArray {
            val attrs = dir.getFileAttributeView(Paths.get(name), BasicFileAttributeView::class.java, NOFOLLOW_LINKS).readAttributes()
            if (!attrs.isRegularFile || attrs.size() !in 1..512) throw IOException("Invalid state file")
            channel(name, setOf(READ, NOFOLLOW_LINKS)).use { input ->
                val size = input.size()
                if (size !in 1..512) throw IOException("Invalid state length")
                val data = ByteBuffer.allocate(size.toInt())
                while (data.hasRemaining() && input.read(data) >= 0) { }
                if (data.hasRemaining() || input.size() != size) throw IOException("State changed while reading")
                input.force(true)
                return data.array()
            }
        }

        override fun createAndSync(name: String, bytes: ByteArray) {
            channel(name, setOf(CREATE_NEW, WRITE, NOFOLLOW_LINKS)).use { output ->
                val data = ByteBuffer.wrap(bytes)
                while (data.hasRemaining()) output.write(data)
                output.force(true)
            }
        }

        override fun syncDirectory() { channel(".", setOf(READ, NOFOLLOW_LINKS)).use { it.force(true) } }
        private fun channel(name: String, options: Set<OpenOption>): FileChannel {
            val channel = dir.newByteChannel(Paths.get(name), options)
            if (channel is FileChannel) return channel
            channel.close()
            throw IOException("Durable file channel unavailable")
        }
        override fun close() = dir.close()

        companion object {
            fun open(path: Path): SecureIo {
                val absolute = path.toAbsolutePath().normalize()
                val expected = checkedIdentity(absolute)
                val opened = Files.newDirectoryStream(absolute)
                if (opened !is SecureDirectoryStream<Path>) {
                    opened.close()
                    throw IOException("Anchored directory operations unavailable")
                }
                try {
                    val actual = opened.getFileAttributeView(BasicFileAttributeView::class.java).readAttributes().fileKey()
                    if (expected != actual || expected != checkedIdentity(absolute)) throw IOException("Directory identity changed")
                    return SecureIo(opened, absolute, expected)
                } catch (t: Throwable) {
                    opened.close()
                    throw t
                }
            }

            private fun checkedIdentity(absolute: Path): Any {
                var current = absolute.root ?: throw IOException("Missing root")
                for (part in absolute) {
                    current = current.resolve(part)
                    if (Files.isSymbolicLink(current) || !Files.isDirectory(current, NOFOLLOW_LINKS))
                        throw IOException("Invalid instance directory")
                }
                val attrs = Files.readAttributes(absolute, BasicFileAttributes::class.java, NOFOLLOW_LINKS)
                return attrs.fileKey() ?: throw IOException("Directory identity unavailable")
            }
        }
    }
}
