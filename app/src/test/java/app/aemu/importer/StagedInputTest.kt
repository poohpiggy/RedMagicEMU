package app.aemu.importer

import org.junit.Assert.*
import org.junit.Test
import java.io.ByteArrayInputStream
import java.io.IOException
import java.nio.file.Files

class StagedInputTest {
    @Test fun copiesInputAndRemovesStageAfterUse() {
        val dir = Files.createTempDirectory("stage-test").toFile()
        try {
            val input = byteArrayOf(1, 2, 3)
            StagedInput.use(dir, ByteArrayInputStream(input), { false }) { file ->
                assertArrayEquals(input, file.readBytes())
                assertTrue(file.canonicalPath.startsWith(dir.canonicalPath + java.io.File.separator))
            }
            assertTrue(dir.listFiles()!!.isEmpty())
        } finally { dir.deleteRecursively() }
    }

    @Test fun cancellationRemovesPartialStage() {
        val dir = Files.createTempDirectory("stage-test").toFile()
        try {
            var checks = 0
            try {
                StagedInput.use(dir, ByteArrayInputStream(ByteArray(2_000_000)), { ++checks > 1 }) { fail("cancelled input was used") }
                fail("expected cancellation")
            } catch (expected: IOException) { assertEquals("cancelled", expected.message) }
            assertTrue(dir.listFiles()!!.isEmpty())
        } finally { dir.deleteRecursively() }
    }

    @Test fun failureInConsumerRemovesStage() {
        val dir = Files.createTempDirectory("stage-test").toFile()
        try {
            try {
                StagedInput.use(dir, ByteArrayInputStream(byteArrayOf(1)), { false }) { throw IOException("bad archive") }
                fail("expected failure")
            } catch (expected: IOException) { assertEquals("bad archive", expected.message) }
            assertTrue(dir.listFiles()!!.isEmpty())
        } finally { dir.deleteRecursively() }
    }

    @Test fun sizeLimitRemovesPartialStage() {
        val dir = Files.createTempDirectory("stage-limit").toFile()
        try {
            try {
                StagedInput.use(dir, ByteArrayInputStream(ByteArray(1024)), { false }, maxBytes = 512) { fail("oversized input was used") }
                fail("expected oversized input rejection")
            } catch (expected: IOException) { assertTrue(expected.message!!.contains("size")) }
            assertTrue(dir.listFiles()!!.isEmpty())
        } finally { dir.deleteRecursively() }
    }
}
