package app.aemu.importer

import java.io.File
import java.io.IOException
import java.nio.file.Files
import java.nio.file.attribute.BasicFileAttributes
import java.util.concurrent.TimeUnit
import org.junit.Assert.*
import org.junit.Assume.assumeNoException
import org.junit.Assume.assumeTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

/** No Guest is launched: all contents are test-created data in a temporary images tree. */
class PendingImportQuarantineTest {
    @get:Rule val temp = TemporaryFolder()

    private fun images() = temp.newFolder("images")
    private fun payload(lease: PendingImports.Lease, id: String = "abcdef") {
        File(lease.dir, id).apply {
            check(mkdir())
            check(File(this, "root").mkdir())
            File(this, "image.json").writeText("complete")
        }
    }

    @Test fun closePreservesAnyExistingQuarantineMarkerAndItsPayload() {
        val images = images()
        val lease = PendingImports.begin(images)
        payload(lease)
        val marker = File(lease.dir, PendingImports.GUEST_QUARANTINE)
        marker.writeText("corrupt and deliberately not trusted")
        val pending = lease.dir
        lease.close()
        assertTrue(marker.exists())
        assertTrue(File(pending, "abcdef/root").isDirectory)
        PendingImports.cleanAbandoned(images)
        assertTrue("an abandoned quarantined import must be retained", pending.isDirectory)
    }

    @Test fun aDirectoryOrDanglingSymlinkAtMarkerPathStillMeansQuarantine() {
        val images = images()
        val first = PendingImports.begin(images)
        val firstDir = first.dir
        check(File(firstDir, PendingImports.GUEST_QUARANTINE).mkdir())
        first.close()
        PendingImports.cleanAbandoned(images)
        assertTrue(firstDir.isDirectory)

        val second = PendingImports.begin(images)
        val secondDir = second.dir
        try {
            Files.createSymbolicLink(File(secondDir, PendingImports.GUEST_QUARANTINE).toPath(),
                File(secondDir, "missing-target").toPath())
        } catch (e: Exception) {
            second.close()
            assumeNoException("symbolic links unavailable", e)
        }
        second.close()
        PendingImports.cleanAbandoned(images)
        assertTrue("a dangling symlink must not be mistaken for an absent marker", secondDir.isDirectory)
    }

    @Test fun existingQuarantineRefusesOrdinaryPublication() {
        val images = images()
        val lease = PendingImports.begin(images)
        payload(lease)
        File(lease.dir, PendingImports.GUEST_QUARANTINE).writeText("x")
        try { lease.commit("abcdef"); fail("quarantined payload must never publish") }
        catch (_: IOException) { }
        lease.close()
        assertFalse(File(images, "abcdef").exists())
        assertTrue(File(lease.dir, "abcdef/root").isDirectory)
    }

    @Test fun quarantineAppearingDuringPublicationPreservesHiddenReservation() {
        val images = images()
        val lease = PendingImports.begin(images)
        payload(lease)
        try {
            lease.commit("abcdef") { phase ->
                if (phase == "populated") File(lease.dir, PendingImports.GUEST_QUARANTINE).writeText("partial")
            }
            fail("quarantine must prevent removing the reservation")
        } catch (_: IOException) { }
        val target = File(images, "abcdef")
        assertTrue(PendingImports.isPublishing(target))
        lease.close()
        PendingImports.cleanAbandoned(images)
        assertTrue("reservation must remain hidden", PendingImports.isPublishing(target))
        assertTrue(File(target, "root").isDirectory)
        assertTrue(lease.dir.isDirectory)
    }

    @Test fun eachInjectedFailureRefusesAuthorizationAndPreservesInProcess() {
        val probe = temp.newFolder("identity-probe")
        assumeTrue("filesystem does not provide stable file identities",
            Files.readAttributes(probe.toPath(), BasicFileAttributes::class.java).fileKey() != null)
        for (fault in PendingImports.QuarantineFault.entries) {
            val images = temp.newFolder("fault_${fault.name}")
            val lease = PendingImports.begin(images)
            payload(lease)
            try { lease.markGuestQuarantineFailingAtForTests(fault); fail("$fault must fail") }
            catch (error: IOException) {
                if (fault != PendingImports.QuarantineFault.AFTER_DIRECTORY_SYNC)
                    assertEquals("$fault must reach the injected step", "Injected quarantine failure", error.message)
            }
            val pending = lease.dir
            lease.close()
            assertTrue("$fault must not delete the live pending tree", pending.isDirectory)
            assertTrue(File(pending, "abcdef/root").isDirectory)
            if (fault != PendingImports.QuarantineFault.BEFORE_CREATE) {
                PendingImports.cleanAbandoned(images)
                assertTrue("$fault marker must prevent later recovery deletion", pending.isDirectory)
            }
        }
    }

    @Test fun successfulQuarantineIsDurableBeforeReturning() {
        val images = images()
        val lease = PendingImports.begin(images)
        val identityAvailable = Files.readAttributes(lease.dir.toPath(), BasicFileAttributes::class.java).fileKey() != null
        if (!identityAvailable) {
            try { lease.markGuestQuarantine(); fail("unverifiable identity must not be authorized") }
            catch (_: IOException) { }
            lease.close()
            assertTrue(lease.dir.isDirectory)
            return
        }
        lease.markGuestQuarantine()
        val marker = File(lease.dir, PendingImports.GUEST_QUARANTINE)
        assertTrue(marker.isFile)
        assertTrue(marker.length() > 0)
        lease.close()
        PendingImports.cleanAbandoned(images)
        assertTrue(lease.dir.isDirectory)
    }

    @Test fun ordinaryAbandonedImportWithoutQuarantineKeepsExistingCleanupBehavior() {
        val images = images()
        val lease = PendingImports.begin(images)
        payload(lease)
        val pending = lease.dir
        lease.close()
        assertFalse(pending.exists())
    }

    @Test fun crashedOwnerCannotCauseQuarantinedHiddenReservationToBeDeleted() {
        val images = images()
        val cp = listOf(PendingQuarantineProcess::class.java, PendingImports::class.java, kotlin.Unit::class.java)
            .map { File(it.protectionDomain.codeSource.location.toURI()).path }
            .distinct().joinToString(File.pathSeparator)
        val child = ProcessBuilder(File(System.getProperty("java.home"), "bin/java").path,
            "-cp", cp, PendingQuarantineProcess::class.java.name, images.path)
            .redirectErrorStream(true).start()
        try {
            val ready = child.inputStream.bufferedReader().readLine()
            assertTrue("child failed: $ready", ready?.startsWith("READY:") == true)
            val pending = File(File(images, ".firmware-pending"), ready!!.removePrefix("READY:"))
            val target = File(images, "abcdef")
            assertTrue(PendingImports.isPublishing(target))
            child.destroyForcibly()
            assertTrue(child.waitFor(10, TimeUnit.SECONDS))
            val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(2)
            var reported: Boolean
            do {
                reported = PendingImports.cleanAbandoned(images)
                if (!reported) Thread.sleep(20)
            } while (!reported && System.nanoTime() < deadline)
            assertTrue("an abandoned quarantined import needs a notice", reported)
            val notice = PendingImports.interruptedNoticeToken(images)
            assertNotNull(notice)
            assertTrue(PendingImports.cleanAbandoned(images))
            assertEquals("the notice must not change while it is awaiting acknowledgement", notice, PendingImports.interruptedNoticeToken(images))
            assertTrue("pending ownership data must remain", pending.isDirectory)
            assertTrue("the reserved target remains hidden", PendingImports.isPublishing(target))
            assertEquals("keep", File(target, "root/guest-writer-data").readText())
        } finally { child.destroyForcibly() }
    }

    @Test fun activeQuarantineDoesNotReportAnInterruptedImport() {
        val images = images()
        val lease = PendingImports.begin(images)
        File(lease.dir, PendingImports.GUEST_QUARANTINE).writeText("unknown")
        assertFalse(PendingImports.cleanAbandoned(images))
        lease.close()
        assertTrue(PendingImports.cleanAbandoned(images))
        assertTrue(lease.dir.isDirectory)
    }

    @Test fun hiddenPreparationRequiresSuccessfulDurableQuarantine() {
        val images = images()
        val lease = PendingImports.begin(images)
        payload(lease)
        File(lease.dir, PendingImports.GUEST_QUARANTINE).writeText("not a durable authorization")
        try { lease.prepareHidden("abcdef"); fail("a marker alone must not authorize preparation") }
        catch (_: IOException) { }
        assertFalse(File(images, "abcdef").exists())
        lease.close()
    }

    @Test fun preparedInstanceRemainsHiddenAndRetained() {
        val images = images()
        val lease = PendingImports.begin(images)
        assumeTrue("filesystem does not provide stable file identities",
            Files.readAttributes(lease.dir.toPath(), BasicFileAttributes::class.java).fileKey() != null)
        payload(lease)
        lease.markGuestQuarantine()
        val target = lease.prepareHidden("abcdef")
        assertTrue(PendingImports.isPublishing(target))
        assertEquals("complete", File(target, "image.json").readText())
        assertTrue(File(target, "root").isDirectory)
        assertFalse(File(lease.dir, "abcdef/image.json").exists())
        lease.close()
        assertTrue(PendingImports.cleanAbandoned(images))
        assertTrue(PendingImports.isPublishing(target))
        assertTrue(File(target, "root").isDirectory)
    }

    @Test fun hiddenPreparationNeverOverwritesExistingPath() {
        val images = images()
        val lease = PendingImports.begin(images)
        assumeTrue("filesystem does not provide stable file identities",
            Files.readAttributes(lease.dir.toPath(), BasicFileAttributes::class.java).fileKey() != null)
        payload(lease)
        lease.markGuestQuarantine()
        File(images, "abcdef").apply { mkdir(); File(this, "user-data").writeText("keep") }
        try { lease.prepareHidden("abcdef"); fail("must not replace an existing path") }
        catch (_: IOException) { }
        lease.close()
        assertEquals("keep", File(images, "abcdef/user-data").readText())
        assertTrue(File(lease.dir, "abcdef/root").isDirectory)
    }

    @Test fun interruptedHiddenPreparationRetainsItsReservation() {
        for (phase in listOf("created", "reserved", "populated")) {
            val images = temp.newFolder("phase_$phase")
            val lease = PendingImports.begin(images)
            assumeTrue("filesystem does not provide stable file identities",
                Files.readAttributes(lease.dir.toPath(), BasicFileAttributes::class.java).fileKey() != null)
            payload(lease)
            lease.markGuestQuarantine()
            try {
                lease.prepareHidden("abcdef") { if (it == phase) throw IOException("injected") }
                fail("interruption must throw")
            } catch (e: IOException) { assertEquals("injected", e.message) }
            lease.close()
            assertTrue(PendingImports.cleanAbandoned(images))
            val target = File(images, "abcdef")
            assertTrue(target.isDirectory)
            if (phase == "created") assertFalse(File(target, "image.json").exists())
            else assertTrue(PendingImports.isPublishing(target))
            assertTrue(lease.dir.isDirectory)
        }
    }

    @Test fun killedHiddenPreparationKeepsItsReservation() {
        val images = images()
        assumeTrue("filesystem does not provide stable file identities",
            Files.readAttributes(images.toPath(), BasicFileAttributes::class.java).fileKey() != null)
        val cp = listOf(PendingQuarantineProcess::class.java, PendingImports::class.java, kotlin.Unit::class.java)
            .map { File(it.protectionDomain.codeSource.location.toURI()).path }
            .distinct().joinToString(File.pathSeparator)
        val child = ProcessBuilder(File(System.getProperty("java.home"), "bin/java").path,
            "-cp", cp, PendingQuarantineProcess::class.java.name, images.path, "prepare")
            .redirectErrorStream(true).start()
        try {
            val ready = child.inputStream.bufferedReader().readLine()
            assertTrue("child failed: $ready", ready?.startsWith("READY:") == true)
            val pending = File(File(images, ".firmware-pending"), ready!!.removePrefix("READY:"))
            val target = File(images, "abcdef")
            assertTrue(PendingImports.isPublishing(target))
            child.destroyForcibly()
            assertTrue(child.waitFor(10, TimeUnit.SECONDS))
            val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(2)
            var reported: Boolean
            do {
                reported = PendingImports.cleanAbandoned(images)
                if (!reported) Thread.sleep(20)
            } while (!reported && System.nanoTime() < deadline)
            assertTrue(reported)
            assertTrue(pending.isDirectory)
            assertTrue(PendingImports.isPublishing(target))
            assertTrue(File(target, "root").isDirectory)
        } finally { child.destroyForcibly() }
    }

    @Test fun changedReservationStateCannotAuthorizePreparation() {
        val images = images()
        val lease = PendingImports.begin(images)
        assumeTrue("filesystem does not provide stable file identities",
            Files.readAttributes(lease.dir.toPath(), BasicFileAttributes::class.java).fileKey() != null)
        payload(lease)
        lease.markGuestQuarantine()
        try {
            lease.prepareHidden("abcdef") { phase ->
                if (phase == "reserved") File(lease.dir, ".publish").writeText("changed")
            }
            fail("changed reservation identity must not be accepted")
        } catch (_: IOException) { }
        lease.close()
        assertTrue(PendingImports.isPublishing(File(images, "abcdef")))
        assertTrue(lease.dir.isDirectory)
        assertTrue(File(lease.dir, "abcdef/root").isDirectory)
    }
}

object PendingQuarantineProcess {
    @JvmStatic fun main(args: Array<String>) {
        val images = File(args[0])
        PendingImports.begin(images).use { lease ->
            if (args.size > 1) {
                File(lease.dir, "abcdef").apply {
                    check(mkdir())
                    check(File(this, "root").mkdir())
                    File(this, "image.json").writeText("complete")
                }
                lease.markGuestQuarantine()
                lease.prepareHidden("abcdef") { phase ->
                    if (phase == "populated") {
                        println("READY:${lease.dir.name}")
                        System.out.flush()
                        System.`in`.read()
                    }
                }
                return
            }
            val target = File(images, "abcdef").apply { check(mkdir()) }
            File(target, "root").apply { check(mkdir()); File(this, "guest-writer-data").writeText("keep") }
            File(target, "image.json").writeText("complete")
            val identity = "abcdef\n${lease.dir.name}\n"
            File(lease.dir, ".publish").writeText(identity)
            File(target, ".redmagic-import-pending").writeText(identity)
            File(lease.dir, PendingImports.GUEST_QUARANTINE).writeText("uncertain")
            println("READY:${lease.dir.name}")
            System.out.flush()
            System.`in`.read()
        }
    }
}
