package app.aemu.core

import java.io.IOException
import java.io.File
import java.nio.ByteBuffer
import java.nio.channels.FileChannel
import java.nio.file.Files
import java.nio.file.LinkOption.NOFOLLOW_LINKS
import java.nio.file.Path
import java.nio.file.StandardOpenOption.*
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import org.junit.Assert.*
import org.junit.Assume
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class FirstRingtoneInitStoreTest {
    @get:Rule val temporary = TemporaryFolder()
    private val bootA = "12345678-1234-4abc-8def-123456789abc"
    private val bootB = "abcdefab-1234-4abc-8def-123456789abc"
    private val bootC = "fedcbafe-1234-4abc-8def-123456789abc"

    private fun recovery(s: FirstRingtoneInitStore, boot: String) = s.claimUncertainRetry(boot).name
    private fun recordBoot(s: FirstRingtoneInitStore, claim: FirstRingtoneInitStore.Claim, boot: String) =
        s.recordInitialBoot(claim, boot)
    /** Real files with a fault-injectable directory barrier; production uses anchored directory operations. */
    private class TestIo(val dir: Path) : FirstRingtoneInitStore.Io {
        @Volatile var failDirectorySync = false
        @Volatile var failOnSync = -1
        @Volatile var syncs = 0
        @Volatile var failCreateName: String? = null
        @Volatile var failCreateStage: String? = null
        @Volatile var failReadName: String? = null
        @Volatile var invalidateIdentityOnWrite: String? = null
        @Volatile var identityValid = true
        override fun verifyIdentity() { if (!identityValid) throw IOException("Injected identity change") }
        override fun exists(name: String) = Files.exists(dir.resolve(name), NOFOLLOW_LINKS)
        override fun readAndSync(name: String): ByteArray {
            if (name == failReadName) throw IOException("Injected read failure")
            val p = dir.resolve(name)
            if (!Files.isRegularFile(p, NOFOLLOW_LINKS)) throw IOException("Invalid state file")
            FileChannel.open(p, READ, NOFOLLOW_LINKS).use { input ->
                if (input.size() > 512) throw IOException("Oversized state")
                val b = ByteBuffer.allocate(input.size().toInt())
                while (b.hasRemaining() && input.read(b) >= 0) { }
                if (b.hasRemaining()) throw IOException("Truncated state")
                input.force(true)
                return b.array()
            }
        }
        override fun createAndSync(name: String, bytes: ByteArray) {
            FileChannel.open(dir.resolve(name), CREATE_NEW, WRITE, NOFOLLOW_LINKS).use { out ->
                if (name == failCreateName && failCreateStage == "created") throw IOException("Injected create failure")
                if (name == failCreateName && failCreateStage == "partial") {
                    out.write(ByteBuffer.wrap(bytes.copyOf(bytes.size / 2)))
                    out.force(true)
                    throw IOException("Injected partial write")
                }
                val b = ByteBuffer.wrap(bytes)
                while (b.hasRemaining()) out.write(b)
                if (name == failCreateName && failCreateStage == "file-sync") throw IOException("Injected file fsync failure")
                out.force(true)
            }
            if (name == invalidateIdentityOnWrite) identityValid = false
        }
        @Synchronized override fun syncDirectory() {
            syncs++
            if (failDirectorySync || syncs == failOnSync) throw IOException("Injected directory fsync failure")
        }
        override fun close() = Unit
    }

    private fun store(io: TestIo) = FirstRingtoneInitStore("abcdef", io)
    private fun tmp(): Path = temporary.newFolder().toPath()

    @Test fun missingStateIsOldInstanceAndNeverClaims() {
        val dir = tmp(); val s = store(TestIo(dir))
        assertEquals(FirstRingtoneInitStore.Status.UNTRACKED, s.status())
        assertNull(s.claim())
    }

    @Test fun freshClaimAndCompletionPersistAcrossReopen() {
        val dir = tmp(); val io = TestIo(dir)
        val first = store(io)
        first.createFresh()
        assertEquals(FirstRingtoneInitStore.Status.FRESH, store(io).status())
        val claim = first.claim()!!
        assertEquals(FirstRingtoneInitStore.Status.IN_PROGRESS, store(io).status())
        first.complete(claim)
        assertEquals(FirstRingtoneInitStore.Status.COMPLETE, store(io).status())
        assertNull(store(io).claim())
    }

    @Test fun legacyInProgressIsBaselinedBeforeAnyAutomaticRetry() {
        val io = TestIo(tmp()); val s = store(io)
        s.createFresh(); assertNotNull(s.claim())
        assertEquals("BASELINED", recovery(s, bootA))
        assertEquals("ALREADY_ATTEMPTED", recovery(store(io), bootA))
        assertEquals("ALLOWED", recovery(store(io), bootB))
        assertEquals("ALREADY_ATTEMPTED", recovery(store(io), bootB))
        assertEquals("ALLOWED", recovery(store(io), bootC))
    }

    @Test fun preparingRecoveryDoesNotConsumeTheNextBootAttempt() {
        val io = TestIo(tmp()); val s = store(io); s.createFresh(); assertNotNull(s.claim())
        fun prepare(boot: String): String = store(io).prepareUncertainRetry(boot).name
        assertEquals("BASELINED", prepare(bootA))
        assertEquals("ALREADY_ATTEMPTED", prepare(bootA))
        assertEquals("ELIGIBLE", prepare(bootB))
        assertEquals("ELIGIBLE", prepare(bootB))
        assertFalse(Files.exists(io.dir.resolve(FirstRingtoneInitStore.RETRY_FILE_PREFIX + bootB)))
        assertEquals("ALLOWED", recovery(store(io), bootB))
        assertEquals("ALREADY_ATTEMPTED", prepare(bootB))
        assertEquals("ELIGIBLE", prepare(bootC))
    }

    @Test fun preparingRejectsUntrackedFreshCompletedAndBadIds() {
        val io = TestIo(tmp()); val s = store(io)
        assertEquals(FirstRingtoneInitStore.RetryPreparation.NOT_IN_PROGRESS, s.prepareUncertainRetry(bootA))
        s.createFresh()
        assertEquals(FirstRingtoneInitStore.RetryPreparation.NOT_IN_PROGRESS, s.prepareUncertainRetry(bootA))
        val claim = s.claim()!!
        assertThrows(IllegalArgumentException::class.java) { s.prepareUncertainRetry(bootA.uppercase()) }
        assertFalse(Files.exists(io.dir.resolve(FirstRingtoneInitStore.RETRY_BASELINE_FILE)))
        s.recordInitialBoot(claim, bootA)
        s.complete(claim)
        assertEquals(FirstRingtoneInitStore.RetryPreparation.NOT_IN_PROGRESS, s.prepareUncertainRetry(bootB))
    }

    @Test fun preparingLegacyClaimCanFailWithoutGrantingRecovery() {
        val io = TestIo(tmp()); val s = store(io); s.createFresh(); s.claim()
        io.failOnSync = io.syncs + 2 // state read, then the new baseline's directory barrier
        assertThrows(IOException::class.java) { s.prepareUncertainRetry(bootA) }
        io.failOnSync = -1
        assertEquals(FirstRingtoneInitStore.RetryPreparation.ALREADY_ATTEMPTED, s.prepareUncertainRetry(bootA))
        assertFalse(Files.exists(io.dir.resolve(FirstRingtoneInitStore.RETRY_FILE_PREFIX + bootA)))
    }

    @Test fun preparingRejectsCorruptedBaseline() {
        val io = TestIo(tmp()); val s = store(io); s.createFresh(); val claim = s.claim()!!
        s.recordInitialBoot(claim, bootA)
        Files.write(io.dir.resolve(FirstRingtoneInitStore.RETRY_BASELINE_FILE), "invalid".toByteArray())
        assertThrows(IOException::class.java) { s.prepareUncertainRetry(bootB) }
        assertFalse(Files.exists(io.dir.resolve(FirstRingtoneInitStore.RETRY_FILE_PREFIX + bootB)))
    }

    @Test fun newClaimRecordsInitialBootAndLaterCompletionStopsRecovery() {
        val io = TestIo(tmp()); val s = store(io)
        s.createFresh(); val claim = s.claim()!!
        recordBoot(s, claim, bootA)
        assertEquals("ALREADY_ATTEMPTED", recovery(store(io), bootA))
        assertEquals("ALLOWED", recovery(store(io), bootB))
        s.complete(claim)
        assertEquals("NOT_IN_PROGRESS", recovery(store(io), bootC))
    }

    @Test fun initialBootRecordingAcceptsMatchingConcurrentLegacyBaseline() {
        val io = TestIo(tmp()); val original = store(io)
        original.createFresh(); val claim = original.claim()!!
        assertEquals(FirstRingtoneInitStore.RetryPreparation.BASELINED, store(io).prepareUncertainRetry(bootA))
        // Another observer recorded the same claim on this boot before the original writer.
        original.recordInitialBoot(claim, bootA)
        assertEquals(FirstRingtoneInitStore.RetryPreparation.ALREADY_ATTEMPTED, original.prepareUncertainRetry(bootA))
        assertEquals(FirstRingtoneInitStore.RetryPreparation.ELIGIBLE, original.prepareUncertainRetry(bootB))
    }

    @Test fun initialBootRecordingRejectsMismatchedConcurrentBaseline() {
        val io = TestIo(tmp()); val original = store(io)
        original.createFresh(); val claim = original.claim()!!
        assertEquals(FirstRingtoneInitStore.RetryPreparation.BASELINED, store(io).prepareUncertainRetry(bootA))
        assertThrows(IOException::class.java) { original.recordInitialBoot(claim, bootB) }
        Files.write(io.dir.resolve(FirstRingtoneInitStore.RETRY_BASELINE_FILE), "corrupt".toByteArray())
        assertThrows(IOException::class.java) { original.recordInitialBoot(claim, bootA) }
    }

    @Test fun matchingConcurrentBaselineStillRequiresReadAndDirectorySync() {
        val io = TestIo(tmp()); val original = store(io)
        original.createFresh(); val claim = original.claim()!!
        assertEquals(FirstRingtoneInitStore.RetryPreparation.BASELINED, store(io).prepareUncertainRetry(bootA))
        io.failReadName = FirstRingtoneInitStore.RETRY_BASELINE_FILE
        assertThrows(IOException::class.java) { original.recordInitialBoot(claim, bootA) }
        io.failReadName = null
        io.failOnSync = io.syncs + 2 // state check, then baseline directory barrier
        assertThrows(IOException::class.java) { original.recordInitialBoot(claim, bootA) }
        io.failOnSync = -1
        original.recordInitialBoot(claim, bootA)
    }

    @Test fun invalidBootIdsCannotCreateOrClaimRecovery() {
        val io = TestIo(tmp()); val s = store(io); s.createFresh(); val claim = s.claim()!!
        for (bad in listOf("", "abc", bootA.uppercase(), bootA + "\n", "12345678-1234-4abc-8def-123456789abz")) {
            assertThrows(IllegalArgumentException::class.java) { s.recordInitialBoot(claim, bad) }
            assertThrows(IllegalArgumentException::class.java) { s.claimUncertainRetry(bad) }
            assertFalse(Files.exists(io.dir.resolve(FirstRingtoneInitStore.RETRY_BASELINE_FILE)))
        }
    }

    @Test fun untrackedFreshAndInvalidInstancesCannotClaimRecovery() {
        val io = TestIo(tmp()); val s = store(io)
        assertEquals("NOT_IN_PROGRESS", recovery(s, bootA))
        s.createFresh()
        assertEquals("NOT_IN_PROGRESS", recovery(s, bootA))
        Files.write(io.dir.resolve(FirstRingtoneInitStore.CLAIM_FILE), byteArrayOf())
        assertEquals("NOT_IN_PROGRESS", recovery(s, bootA))
        assertFalse(Files.exists(io.dir.resolve(FirstRingtoneInitStore.RETRY_BASELINE_FILE)))
    }

    @Test fun concurrentRecoveryOnSameBootHasOneWinner() {
        val io = TestIo(tmp()); val s = store(io); s.createFresh(); val claim = s.claim()!!
        recordBoot(s, claim, bootA)
        val pool = Executors.newFixedThreadPool(8); val start = CountDownLatch(1)
        try {
            val tasks = (1..8).map { pool.submit<String> { start.await(); recovery(store(io), bootB) } }
            start.countDown()
            val results = tasks.map { it.get(10, TimeUnit.SECONDS) }
            assertEquals(1, results.count { it == "ALLOWED" })
            assertEquals(7, results.count { it == "ALREADY_ATTEMPTED" })
        } finally { pool.shutdownNow() }
    }

    @Test fun separateProcessesCannotBothClaimRecoveryForSameBoot() {
        val dir = tmp(); val io = TestIo(dir); val s = store(io)
        s.createFresh(); val claim = s.claim()!!; recordBoot(s, claim, bootA)
        val classpath = listOf(FirstRingtoneClaimChild::class.java, FirstRingtoneInitStore::class.java, kotlin.Unit::class.java)
            .map { File(it.protectionDomain.codeSource.location.toURI()).absolutePath }
            .distinct().joinToString(File.pathSeparator)
        val java = File(System.getProperty("java.home"), "bin/java").absolutePath
        val children = (1..2).map {
            ProcessBuilder(java, "-cp", classpath, FirstRingtoneClaimChild::class.java.name, dir.toString(), bootB)
                .redirectErrorStream(true).start()
        }
        val readers = children.map { it.inputStream.bufferedReader() }
        val pool = Executors.newFixedThreadPool(2)
        try {
            assertEquals(listOf("READY", "READY"), readers.map { reader -> pool.submit<String?> { reader.readLine() } }
                .map { it.get(10, TimeUnit.SECONDS) })
            children.forEach { it.outputStream.write(1); it.outputStream.flush() }
            val outcomes = readers.map { reader -> pool.submit<String?> { reader.readLine() } }
                .map { it.get(10, TimeUnit.SECONDS) }
            assertEquals(listOf("ALLOWED", "ALREADY_ATTEMPTED"), outcomes.sorted())
            children.forEach { assertTrue(it.waitFor(10, TimeUnit.SECONDS)); assertEquals(0, it.exitValue()) }
        } finally {
            children.forEach { if (it.isAlive) it.destroyForcibly() }
            pool.shutdownNow()
        }
    }

    @Test fun failedRetryRecordCreationNeverGrantsAnotherAttemptInSameBoot() {
        for (stage in listOf("created", "partial", "file-sync")) {
            val io = TestIo(tmp()); val s = store(io); s.createFresh(); val claim = s.claim()!!
            recordBoot(s, claim, bootA)
            io.failCreateName = FirstRingtoneInitStore.RETRY_FILE_PREFIX + bootB
            io.failCreateStage = stage
            assertThrows(IOException::class.java) { s.claimUncertainRetry(bootB) }
            io.failCreateName = null
            assertEquals("ALREADY_ATTEMPTED", recovery(store(io), bootB))
        }
    }

    @Test fun retryDirectorySyncFailureNeverGrantsAttemptInSameBoot() {
        val io = TestIo(tmp()); val s = store(io); s.createFresh(); val claim = s.claim()!!
        recordBoot(s, claim, bootA)
        // Preparation checks and the claim's state check precede retry creation.
        io.failOnSync = io.syncs + 5
        assertThrows(IOException::class.java) { s.claimUncertainRetry(bootB) }
        assertTrue(Files.exists(io.dir.resolve(FirstRingtoneInitStore.RETRY_FILE_PREFIX + bootB)))
        io.failOnSync = -1
        assertEquals("ALREADY_ATTEMPTED", recovery(store(io), bootB))
    }

    @Test fun invalidBaselineAndChangedIdentityNeverPermitRetry() {
        val io = TestIo(tmp()); val s = store(io); s.createFresh(); val claim = s.claim()!!
        recordBoot(s, claim, bootA)
        Files.write(io.dir.resolve(FirstRingtoneInitStore.RETRY_BASELINE_FILE), "not a bound record".toByteArray())
        assertThrows(IOException::class.java) { s.claimUncertainRetry(bootB) }
        assertFalse(Files.exists(io.dir.resolve(FirstRingtoneInitStore.RETRY_FILE_PREFIX + bootB)))
    }

    @Test fun changedIdentityWhileWritingRetryCannotAuthorizeScan() {
        val io = TestIo(tmp()); val s = store(io); s.createFresh(); val claim = s.claim()!!
        recordBoot(s, claim, bootA)
        io.invalidateIdentityOnWrite = FirstRingtoneInitStore.RETRY_FILE_PREFIX + bootB
        assertThrows(IOException::class.java) { s.claimUncertainRetry(bootB) }
        io.identityValid = true
        assertEquals("ALREADY_ATTEMPTED", recovery(store(io), bootB))
    }

    @Test fun baselineIsBoundToClaimAndCannotBeReusedForAnotherInstance() {
        val a = TestIo(tmp()); val first = store(a); first.createFresh(); val claim = first.claim()!!
        recordBoot(first, claim, bootA)
        val b = TestIo(tmp()); val second = FirstRingtoneInitStore("bcdefg", b)
        second.createFresh(); second.claim()
        Files.copy(a.dir.resolve(FirstRingtoneInitStore.RETRY_BASELINE_FILE), b.dir.resolve(FirstRingtoneInitStore.RETRY_BASELINE_FILE))
        assertThrows(IOException::class.java) { second.claimUncertainRetry(bootB) }
        assertFalse(Files.exists(b.dir.resolve(FirstRingtoneInitStore.RETRY_FILE_PREFIX + bootB)))
    }

    @Test fun observedDefaultsCanCompleteAClaimAfterReopenWithoutAnotherClaim() {
        val io = TestIo(tmp()); val original = store(io)
        original.createFresh()
        assertNotNull(original.claim())
        val resumed = store(io)
        assertTrue(resumed.completeObservedIfInProgress())
        assertEquals(FirstRingtoneInitStore.Status.COMPLETE, store(io).status())
        assertNull(store(io).claim())
    }

    @Test fun observedCompletionPropagatesStateReadFailures() {
        val io = TestIo(tmp()); val s = store(io)
        s.createFresh(); s.claim()
        io.failReadName = FirstRingtoneInitStore.CLAIM_FILE
        assertThrows(IOException::class.java) { s.completeObservedIfInProgress() }
        io.failReadName = null
        assertEquals(FirstRingtoneInitStore.Status.IN_PROGRESS, s.status())
    }

    @Test fun observationCannotCompleteUntrackedFreshOrMalformedState() {
        val io = TestIo(tmp()); val s = store(io)
        assertFalse(s.completeObservedIfInProgress())
        s.createFresh()
        assertFalse(s.completeObservedIfInProgress())
        Files.write(io.dir.resolve(FirstRingtoneInitStore.CLAIM_FILE), byteArrayOf())
        assertFalse(s.completeObservedIfInProgress())
        assertEquals(FirstRingtoneInitStore.Status.INVALID, s.status())
    }

    @Test fun concurrentObservedCompletionCreatesOnlyTheMatchingDoneRecord() {
        val io = TestIo(tmp()); store(io).apply { createFresh(); claim() }
        val pool = Executors.newFixedThreadPool(2); val start = CountDownLatch(1)
        try {
            val results = (1..2).map { pool.submit<Boolean> { start.await(); store(io).completeObservedIfInProgress() } }
            start.countDown()
            results.forEach { it.get(10, TimeUnit.SECONDS) }
            assertEquals(FirstRingtoneInitStore.Status.COMPLETE, store(io).status())
            assertNull(store(io).claim())
        } finally { pool.shutdownNow() }
    }

    @Test fun concurrentClaimHasExactlyOneWinner() {
        val dir = tmp(); val io = TestIo(dir); store(io).createFresh()
        val pool = Executors.newFixedThreadPool(2); val start = CountDownLatch(1)
        try {
            val results = (1..2).map { pool.submit<Boolean> { start.await(); store(io).claim() != null } }
            start.countDown()
            assertEquals(1, results.count { it.get(10, TimeUnit.SECONDS) })
            assertEquals(FirstRingtoneInitStore.Status.IN_PROGRESS, store(io).status())
        } finally { pool.shutdownNow() }
    }

    @Test fun separateProcessesCannotBothClaimSameInstance() {
        val dir = tmp(); store(TestIo(dir)).createFresh()
        val classpath = listOf(FirstRingtoneClaimChild::class.java, FirstRingtoneInitStore::class.java, kotlin.Unit::class.java)
            .map { File(it.protectionDomain.codeSource.location.toURI()).absolutePath }
            .distinct().joinToString(File.pathSeparator)
        val java = File(System.getProperty("java.home"), "bin/java").absolutePath
        val children = (1..2).map {
            ProcessBuilder(java, "-cp", classpath, FirstRingtoneClaimChild::class.java.name, dir.toString())
                .redirectErrorStream(true).start()
        }
        val readers = children.map { it.inputStream.bufferedReader() }
        val pool = Executors.newFixedThreadPool(2)
        try {
            val ready = readers.map { reader -> pool.submit<String?> { reader.readLine() } }
            assertEquals(listOf("READY", "READY"), ready.map { it.get(10, TimeUnit.SECONDS) })
            children.forEach { it.outputStream.write(1); it.outputStream.flush() }
            val outcomes = readers.map { reader -> pool.submit<String?> { reader.readLine() } }
                .map { it.get(10, TimeUnit.SECONDS) }
            assertEquals(listOf("LOST", "WON"), outcomes.sorted())
            children.forEach { assertTrue(it.waitFor(10, TimeUnit.SECONDS)); assertEquals(0, it.exitValue()) }
            assertEquals(FirstRingtoneInitStore.Status.IN_PROGRESS, store(TestIo(dir)).status())
        } finally {
            children.forEach { if (it.isAlive) it.destroyForcibly() }
            pool.shutdownNow()
        }
    }

    @Test fun failedClaimSyncNeverReturnsPermissionOrRetries() {
        val dir = tmp(); val io = TestIo(dir); val s = store(io); s.createFresh()
        io.failOnSync = io.syncs + 2 // claim checks fresh, then must sync the new claim
        assertThrows(IOException::class.java) { s.claim() }
        io.failOnSync = -1
        assertNotEquals(FirstRingtoneInitStore.Status.FRESH, store(io).status())
        assertNull(store(io).claim())
    }

    @Test fun interruptedAndMalformedFilesNeverBecomeFresh() {
        val dir = tmp(); val io = TestIo(dir); val s = store(io); s.createFresh()
        Files.write(dir.resolve(FirstRingtoneInitStore.CLAIM_FILE), byteArrayOf())
        assertEquals(FirstRingtoneInitStore.Status.INVALID, store(io).status())
        assertNull(store(io).claim())
    }

    @Test fun claimFileCreationPartialWriteAndFileSyncFailureNeverPermitRetry() {
        for (stage in listOf("created", "partial", "file-sync")) {
            val io = TestIo(tmp()); val first = store(io); first.createFresh()
            io.failCreateName = FirstRingtoneInitStore.CLAIM_FILE
            io.failCreateStage = stage
            assertThrows(IOException::class.java) { first.claim() }
            io.failCreateName = null
            assertNotEquals("$stage must never recover as fresh", FirstRingtoneInitStore.Status.FRESH, store(io).status())
            assertNull(store(io).claim())
        }
    }

    @Test fun completionDirectorySyncFailureAfterMarkerCreationNeverRetriesClaim() {
        val io = TestIo(tmp()); val s = store(io); s.createFresh(); val claim = s.claim()!!
        io.failOnSync = io.syncs + 2 // complete checks in-progress, creates DONE, then syncs directory
        assertThrows(IOException::class.java) { s.complete(claim) }
        assertTrue(Files.exists(io.dir.resolve(FirstRingtoneInitStore.DONE_FILE), NOFOLLOW_LINKS))
        io.failOnSync = -1
        // Recovery may accept the valid marker after re-syncing it; it must never allow a second claim.
        assertEquals(FirstRingtoneInitStore.Status.COMPLETE, store(io).status())
        assertNull(store(io).claim())
    }

    @Test fun identityReplacementWhileClaimIsWrittenNeverReturnsPermission() {
        val io = TestIo(tmp()); val s = store(io); s.createFresh()
        io.invalidateIdentityOnWrite = FirstRingtoneInitStore.CLAIM_FILE
        assertThrows(IOException::class.java) { s.claim() }
    }

    @Test fun identityReplacementWhileCompletionIsWrittenNeverReportsSuccess() {
        val io = TestIo(tmp()); val s = store(io); s.createFresh(); val claim = s.claim()!!
        io.invalidateIdentityOnWrite = FirstRingtoneInitStore.DONE_FILE
        assertThrows(IOException::class.java) { s.complete(claim) }
    }

    @Test fun missingFreshOrCopiedIdentityCannotAuthorizeWrite() {
        val dir = tmp(); val io = TestIo(dir); val s = store(io); s.createFresh(); s.claim()
        Files.delete(dir.resolve(FirstRingtoneInitStore.FRESH_FILE))
        assertEquals(FirstRingtoneInitStore.Status.INVALID, store(io).status())
        assertNull(store(io).claim())
        val other = tmp(); val otherIo = TestIo(other); store(otherIo).createFresh()
        assertEquals(FirstRingtoneInitStore.Status.INVALID, FirstRingtoneInitStore("bcdefg", otherIo).status())
    }

    @Test fun invalidCompletionOrFailureDoesNotPermitRetry() {
        val dir = tmp(); val io = TestIo(dir); val s = store(io); s.createFresh()
        val claim = s.claim()!!
        io.failDirectorySync = true
        assertThrows(IOException::class.java) { s.complete(claim) }
        assertThrows(IOException::class.java) { s.status() }
        io.failDirectorySync = false
        assertNotEquals(FirstRingtoneInitStore.Status.FRESH, store(io).status())
        assertNull(store(io).claim())
    }

    @Test fun createFreshRefusesAllPreexistingState() {
        val dir = tmp(); val io = TestIo(dir); val s = store(io); s.createFresh(); s.claim()
        assertThrows(IOException::class.java) { s.createFresh() }
        assertEquals(FirstRingtoneInitStore.Status.IN_PROGRESS, s.status())
    }

    @Test fun rejectsSymlinkedInstanceDirectory() {
        val parent = tmp(); val actual = Files.createDirectory(parent.resolve("actual"))
        val link = parent.resolve("abcdef")
        try { Files.createSymbolicLink(link, actual) } catch (e: Exception) { Assume.assumeNoException(e) }
        assertThrows(IOException::class.java) { FirstRingtoneInitStore.open(link.toFile(), "abcdef") }
        assertEquals(0L, Files.list(actual).use { it.count() })
    }

    @Test fun directoryReplacementAfterOpenMustNotAuthorizeClaimForOldInode() {
        val parent = tmp(); val original = Files.createDirectory(parent.resolve("abcdef"))
        val stream = Files.newDirectoryStream(original)
        val supported = stream is java.nio.file.SecureDirectoryStream<*>
        stream.close()
        Assume.assumeTrue("filesystem lacks anchored directory operations", supported)
        FirstRingtoneInitStore.open(original.toFile(), "abcdef").use { old ->
            old.createFresh()
            Files.move(original, parent.resolve("old-instance"))
            Files.createDirectory(original)
            // Any successful claim must refer to the instance still at the original path.
            assertThrows(IOException::class.java) { old.claim() }
        }
    }
}

/** Child JVM uses real CREATE_NEW files; directory fsync is intentionally not simulated on Windows. */
internal object FirstRingtoneClaimChild {
    @JvmStatic fun main(args: Array<String>) {
        val dir = java.nio.file.Paths.get(args.first())
        val io = object : FirstRingtoneInitStore.Io {
            override fun verifyIdentity() = Unit
            override fun exists(name: String) = Files.exists(dir.resolve(name), NOFOLLOW_LINKS)
            override fun readAndSync(name: String): ByteArray {
                val path = dir.resolve(name)
                if (!Files.isRegularFile(path, NOFOLLOW_LINKS)) throw IOException("Invalid state")
                return FileChannel.open(path, READ, NOFOLLOW_LINKS).use { input ->
                    val data = ByteBuffer.allocate(input.size().toInt())
                    while (data.hasRemaining() && input.read(data) >= 0) { }
                    if (data.hasRemaining()) throw IOException("Short state")
                    input.force(true)
                    data.array()
                }
            }
            override fun createAndSync(name: String, bytes: ByteArray) {
                FileChannel.open(dir.resolve(name), CREATE_NEW, WRITE, NOFOLLOW_LINKS).use { out ->
                    val data = ByteBuffer.wrap(bytes)
                    while (data.hasRemaining()) out.write(data)
                    out.force(true)
                }
            }
            override fun syncDirectory() = Unit
            override fun close() = Unit
        }
        println("READY")
        System.out.flush()
        check(System.`in`.read() >= 0)
        val store = FirstRingtoneInitStore("abcdef", io)
        println(if (args.size == 1) { if (store.claim() == null) "LOST" else "WON" }
            else store.claimUncertainRetry(args[1]).name)
    }
}
