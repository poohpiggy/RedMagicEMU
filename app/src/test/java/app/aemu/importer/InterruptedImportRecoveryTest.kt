package app.aemu.importer

import org.junit.Assert.*
import org.junit.Test
import java.io.File
import java.nio.file.Files
import java.util.concurrent.TimeUnit

class InterruptedImportRecoveryTest {
    @Test fun aPreviouslyFailedGracefulCleanupIsNotReportedAsProcessInterruption() {
        val images = Files.createTempDirectory("recovery-closing").toFile()
        try {
            PendingImports.begin(images).use { live ->
                val stale = File(live.dir.parentFile, "pending-00000000000000000000000000000000").apply { mkdir() }
                File(live.dir, ".owner").copyTo(File(stale, ".owner"))
                File(stale, ".lease").writeBytes(byteArrayOf())
                File(stale, ".closing").writeBytes(byteArrayOf())
                File(stale, "partial").writeText("partial")
                assertEquals(false, PendingImports.cleanAbandoned(images))
                assertFalse(stale.exists())
            }
        } finally { images.deleteRecursively() }
    }
    @Test fun abandonedUnpublishedImportIsReportedAfterCleanup() {
        val images = Files.createTempDirectory("recovery-notice").toFile()
        var child: Process? = null
        try {
            child = launch(images)
            val line = child.inputStream.bufferedReader().readLine()
            assertTrue(line?.startsWith("READY:") == true)
            assertEquals(false, PendingImports.cleanAbandoned(images) as Any)
            child.destroyForcibly()
            assertTrue(child.waitFor(10, TimeUnit.SECONDS))
            var reported = false
            val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(3)
            do {
                reported = PendingImports.cleanAbandoned(images)
                if (!reported) Thread.sleep(20)
            } while (!reported && System.nanoTime() < deadline)
            assertTrue(reported)
            // A second recovery simulates the app dying before the notice was displayed.
            assertEquals(true, PendingImports.cleanAbandoned(images) as Any)
            val token = PendingImports.interruptedNoticeToken(images)!!
            assertTrue(PendingImports.acknowledgeInterrupted(images, token))
            assertEquals(false, PendingImports.cleanAbandoned(images) as Any)
        } finally { child?.destroyForcibly(); images.deleteRecursively() }
    }

    @Test fun acknowledgementOfOldNoticeDoesNotDeleteNewInterruption() {
        val images = Files.createTempDirectory("recovery-notice-race").toFile()
        try {
            PendingImports.begin(images).use { live ->
                fun stale(name: String) {
                    val dir = File(live.dir.parentFile, "pending-$name").apply { mkdir() }
                    File(live.dir, ".owner").copyTo(File(dir, ".owner"))
                    File(dir, ".lease").writeBytes(byteArrayOf())
                    File(dir, "partial").writeText("partial")
                }
                stale("00000000000000000000000000000000")
                assertTrue(PendingImports.cleanAbandoned(images))
                val old = PendingImports.interruptedNoticeToken(images)!!
                stale("11111111111111111111111111111111")
                assertTrue(PendingImports.cleanAbandoned(images))
                val latest = PendingImports.interruptedNoticeToken(images)!!
                assertNotEquals(old, latest)
                assertFalse(PendingImports.acknowledgeInterrupted(images, old))
                assertEquals(latest, PendingImports.interruptedNoticeToken(images))
                assertTrue(PendingImports.acknowledgeInterrupted(images, latest))
            }
        } finally { images.deleteRecursively() }
    }

    @Test fun alreadyPublishedInstanceIsNotReportedAsInterrupted() {
        val images = Files.createTempDirectory("recovery-published").toFile()
        var child: Process? = null
        try {
            child = launch(images, "published")
            assertEquals("READY:published", child.inputStream.bufferedReader().readLine())
            child.destroyForcibly()
            assertTrue(child.waitFor(10, TimeUnit.SECONDS))
            assertEquals(false, PendingImports.cleanAbandoned(images) as Any)
            assertTrue(File(images, "bcdefg/image.json").isFile)
        } finally { child?.destroyForcibly(); images.deleteRecursively() }
    }

    private fun launch(images: File, phase: String? = null): Process {
        val cp = listOf(PendingLeaseProcess::class.java, PendingImports::class.java, kotlin.Unit::class.java)
            .map { File(it.protectionDomain.codeSource.location.toURI()).path }.distinct().joinToString(File.pathSeparator)
        val args = listOf(File(System.getProperty("java.home"), "bin/java").path, "-cp", cp,
            PendingLeaseProcess::class.java.name, images.path) + listOfNotNull(phase)
        return ProcessBuilder(args).redirectErrorStream(true).start()
    }
}
