package app.aemu.importer

import java.io.File
import java.nio.file.Files
import org.junit.Assert.*
import org.junit.Test

class PendingImportsTest {
    @Test fun cancellationBeforePublicationMustNotPublishInstance() {
        val images = Files.createTempDirectory("images").toFile()
        try {
            val gate = PublicationGate()
            PendingImports.begin(images).use { lease ->
                File(lease.dir, "abcdef").apply { mkdir(); File(this, "root").mkdir(); File(this, "image.json").writeText("complete") }
                try {
                    lease.commit("abcdef", publish = gate::publish) { phase -> if (phase == "populated") gate.setCancelled(true) }
                    fail("cancellation before publication must prevent publication")
                } catch (_: java.io.IOException) { }
                assertTrue(gate.cancelled)
            }
            assertFalse("cancelled instance must be cleaned", File(images, "abcdef").exists())
        } finally { images.deleteRecursively() }
    }
    @Test fun publicationThatAlreadyStartedWinsAgainstLaterCancellation() {
        val images = Files.createTempDirectory("images").toFile()
        try {
            val gate = PublicationGate()
            PendingImports.begin(images).use { lease ->
                File(lease.dir, "abcdef").apply { mkdir(); File(this, "root").mkdir(); File(this, "image.json").writeText("complete") }
                lease.commit("abcdef", publish = gate::publish) { phase -> if (phase == "published") gate.setCancelled(true) }
            }
            assertFalse(gate.cancelled)
            assertTrue(File(images, "abcdef/image.json").isFile)
        } finally { images.deleteRecursively() }
    }
    @Test fun secondPublicationIsRejectedUnderTheImagesLock() {
        val images = Files.createTempDirectory("images").toFile()
        try {
            PendingImports.begin(images).use { first ->
                PendingImports.begin(images).use { second ->
                    for ((lease, id) in listOf(first to "abcdef", second to "bcdefg")) {
                        File(lease.dir, id).apply { mkdir(); File(this, "root").mkdir(); File(this, "image.json").writeText("complete") }
                    }
                    first.commit("abcdef")
                    try { second.commit("bcdefg"); fail("a second instance must not be published") }
                    catch (_: java.io.IOException) { }
                    assertTrue(File(images, "abcdef/image.json").isFile)
                    assertFalse(File(images, "bcdefg").exists())
                }
            }
        } finally { images.deleteRecursively() }
    }
    @Test fun committedInstanceSurvivesCleanupAndCannotReplaceExistingInstance() {
        val images = Files.createTempDirectory("images").toFile()
        try {
            val existing = File(images, "abcdef").apply { mkdir(); File(this, "userdata").writeText("keep") }
            PendingImports.begin(images).use { pending ->
                val completed = File(pending.dir, "bcdefg").apply { mkdir() }
                File(completed, "root").mkdir()
                File(completed, "image.json").writeText("complete")
                val conflicting = File(pending.dir, "abcdef").apply { mkdir() }
                File(conflicting, "root").mkdir()
                File(conflicting, "image.json").writeText("conflict")
                try { pending.commit("abcdef"); fail("must not replace existing instance") } catch (_: java.io.IOException) { }
                assertEquals("keep", File(existing, "userdata").readText())
                pending.commit("bcdefg")
            }
            PendingImports.cleanAbandoned(images)
            assertEquals("complete", File(images, "bcdefg/image.json").readText())
            assertEquals("keep", File(existing, "userdata").readText())
        } finally { images.deleteRecursively() }
    }
    @Test fun cleanupKeepsLiveImportAndUnknownDirectoryButRemovesAbandonedMarkedImport() {
        val images = Files.createTempDirectory("images").toFile()
        try {
            PendingImports.begin(images).use { live ->
                File(live.dir, "incomplete").writeText("live")
                val parent = live.dir.parentFile
                val stale = File(parent, "pending-00000000000000000000000000000000").apply { mkdir() }
                File(live.dir, ".owner").copyTo(File(stale, ".owner"))
                File(stale, ".lease").writeBytes(byteArrayOf())
                File(stale, "incomplete").writeText("stale")
                val unknown = File(parent, "pending-11111111111111111111111111111111").apply { mkdir() }
                File(unknown, "user-data").writeText("keep")
                PendingImports.cleanAbandoned(images)
                assertTrue(File(live.dir, "incomplete").isFile)
                assertFalse(stale.exists())
                assertEquals("keep", File(unknown, "user-data").readText())
            }
        } finally { images.deleteRecursively() }
    }
    @Test fun emptyDirectoriesLeftByEarlyCrashCanBeRemovedWithoutTouchingUnknownData() {
        val images = Files.createTempDirectory("images").toFile()
        try {
            PendingImports.begin(images).use { live ->
                val parent = live.dir.parentFile
                val empty = File(parent, "pending-33333333333333333333333333333333").apply { mkdir() }
                val leaseOnly = File(parent, "pending-44444444444444444444444444444444").apply { mkdir(); File(this, ".lease").writeBytes(byteArrayOf()) }
                val unknown = File(parent, "pending-55555555555555555555555555555555").apply { mkdir(); File(this, "user-data").writeText("keep") }
                val partialMarker = File(parent, "pending-66666666666666666666666666666666").apply { mkdir(); File(this, ".lease").writeBytes(byteArrayOf()); File(this, ".owner").writeText("RedMagicEMU") }
                PendingImports.cleanAbandoned(images)
                assertFalse(empty.exists())
                assertFalse(leaseOnly.exists())
                assertFalse(partialMarker.exists())
                assertEquals("keep", File(unknown, "user-data").readText())
            }
        } finally { images.deleteRecursively() }
    }
    @Test fun failedImportRemovesOnlyItsOwnPendingTree() {
        val images = Files.createTempDirectory("images").toFile()
        try {
            val lease = PendingImports.begin(images)
            val dir = lease.dir
            File(dir, "partial").writeText("partial")
            lease.close()
            assertFalse(dir.exists())
        } finally { images.deleteRecursively() }
    }
    @Test fun killedPublishRemovesOnlyOwnedUnpublishedReservations() {
        for (phase in listOf("pre-marker", "reserved", "populated", "published")) {
            val images = Files.createTempDirectory("images").toFile()
            var process: Process? = null
            try {
                val keep = File(images, "abcdef").apply { mkdir(); File(this, "user-data").writeText("keep") }
                val cp = listOf(PendingLeaseProcess::class.java, PendingImports::class.java, kotlin.Unit::class.java)
                    .map { File(it.protectionDomain.codeSource.location.toURI()).path }.distinct().joinToString(File.pathSeparator)
                val javaCommand = File(System.getProperty("java.home"), "bin/java").path
                process = ProcessBuilder(javaCommand, "-cp", cp, PendingLeaseProcess::class.java.name, images.path, phase)
                    .redirectErrorStream(true).start()
                val line = process.inputStream.bufferedReader().readLine()
                assertEquals("READY:$phase", line)
                val target = File(images, "bcdefg")
                assertTrue(target.isDirectory)
                assertEquals(phase == "reserved" || phase == "populated", PendingImports.isPublishing(target))
                process.destroyForcibly()
                assertTrue(process.waitFor(10, java.util.concurrent.TimeUnit.SECONDS))
                val deadline = System.nanoTime() + java.util.concurrent.TimeUnit.SECONDS.toNanos(2)
                do {
                    PendingImports.cleanAbandoned(images)
                    if ((phase == "reserved" || phase == "populated") && target.exists()) Thread.sleep(20)
                } while ((phase == "reserved" || phase == "populated") && target.exists() && System.nanoTime() < deadline)
                if (phase == "published") assertEquals("complete", File(target, "image.json").readText())
                else if (phase == "pre-marker") assertTrue("unmarked target must be preserved", target.isDirectory)
                else assertFalse("unpublished target must be removed", target.exists())
                assertEquals("keep", File(keep, "user-data").readText())
            } finally { process?.destroyForcibly(); images.deleteRecursively() }
        }
    }
    @Test fun staleCleanupNeverFollowsGuestLinks() {
        val images = Files.createTempDirectory("images").toFile()
        val outside = Files.createTempDirectory("outside").toFile()
        try {
            File(outside, "keep").writeText("original")
            PendingImports.begin(images).use { live ->
                val stale = File(live.dir.parentFile, "pending-22222222222222222222222222222222").apply { mkdir() }
                File(live.dir, ".owner").copyTo(File(stale, ".owner"))
                File(stale, ".lease").writeBytes(byteArrayOf())
                try { Files.createSymbolicLink(File(stale, "escape").toPath(), outside.toPath()) }
                catch (e: java.nio.file.FileSystemException) { org.junit.Assume.assumeNoException(e) }
                PendingImports.cleanAbandoned(images)
                assertFalse(stale.exists())
                assertEquals("original", File(outside, "keep").readText())
            }
        } finally { images.deleteRecursively(); outside.deleteRecursively() }
    }
    @Test fun deadProcessLeaseIsCleanedWithoutTouchingLiveInstance() {
        val images = Files.createTempDirectory("images").toFile()
        var process: Process? = null
        try {
            val existing = File(images, "abcdef").apply { mkdir(); File(this, "userdata").writeText("keep") }
            val cp = listOf(PendingLeaseProcess::class.java, PendingImports::class.java, kotlin.Unit::class.java)
                .map { File(it.protectionDomain.codeSource.location.toURI()).path }.distinct().joinToString(File.pathSeparator)
            val javaCommand = File(System.getProperty("java.home"), "bin/java").path
            process = ProcessBuilder(javaCommand, "-cp", cp, PendingLeaseProcess::class.java.name, images.path)
                .redirectErrorStream(true).start()
            val line = process.inputStream.bufferedReader().readLine()
            assertTrue("child failed: $line", line?.startsWith("READY:") == true)
            val pending = File(File(images, ".firmware-pending"), line!!.removePrefix("READY:"))
            assertTrue(File(pending, "partial").isFile)
            PendingImports.cleanAbandoned(images)
            assertTrue("live process lease must remain", pending.isDirectory)
            process.destroyForcibly()
            assertTrue(process.waitFor(10, java.util.concurrent.TimeUnit.SECONDS))
            val deadline = System.nanoTime() + java.util.concurrent.TimeUnit.SECONDS.toNanos(2)
            do {
                PendingImports.cleanAbandoned(images)
                if (pending.exists()) Thread.sleep(20)
            } while (pending.exists() && System.nanoTime() < deadline)
            assertFalse("dead process lease should be removed", pending.exists())
            assertEquals("keep", File(existing, "userdata").readText())
        } finally { process?.destroyForcibly(); images.deleteRecursively() }
    }
}

object PendingLeaseProcess {
    @JvmStatic fun main(args: Array<String>) {
        PendingImports.begin(File(args[0])).use { lease ->
            if (args.size == 1) {
                File(lease.dir, "partial").writeText("partial")
                println("READY:${lease.dir.name}")
                System.out.flush()
                System.`in`.read()
            } else {
                val image = File(lease.dir, "bcdefg").apply { mkdir(); File(this, "root").mkdir() }
                File(image, "image.json").writeText("complete")
                lease.commit("bcdefg") { phase ->
                    if (phase == args[1]) { println("READY:$phase"); System.out.flush(); System.`in`.read() }
                }
            }
        }
    }
}
