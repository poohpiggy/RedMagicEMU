package app.aemu.importer

import org.junit.Assert.*
import org.junit.Test
import java.io.ByteArrayInputStream
import java.io.IOException
import java.nio.file.Files

class ImportCancelSignalTest {
    @Test fun cancelledStagingHasDistinctExceptionAfterCleanup() {
        val parent = Files.createTempDirectory("cancel-signal").toFile()
        try {
            val error = try {
                StagedInput.use(parent, ByteArrayInputStream(ByteArray(1)), { true }) { }
                fail("Expected cancellation")
                return
            } catch (e: IOException) { e }
            assertEquals("ImportCancelledException", error.javaClass.simpleName)
            assertTrue(parent.listFiles().orEmpty().isEmpty())
        } finally { parent.deleteRecursively() }
    }

    @Test fun cancellationAtPublishHasDistinctException() {
        val gate = PublicationGate()
        gate.setCancelled(true)
        val error = try {
            gate.publish { fail("Must not publish") }
            fail("Expected cancellation")
            return
        } catch (e: IOException) { e }
        assertEquals("ImportCancelledException", error.javaClass.simpleName)
    }
}
