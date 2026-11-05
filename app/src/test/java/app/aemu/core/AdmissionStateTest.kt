package app.aemu.core

import org.junit.Assert.*
import org.junit.Test

class AdmissionStateTest {
    @Test fun failedAdmissionNeverPersistsLogToUntrustedPath() {
        val gate = AdmissionState()
        val directory = java.nio.file.Files.createTempDirectory("failed-admission-log").toFile()
        try {
            val target = directory.resolve("external.log").apply { writeText("unchanged") }
            val log = directory.resolve("aemu.log")
            try { java.nio.file.Files.createSymbolicLink(log.toPath(), target.toPath()) }
            catch (e: java.nio.file.FileSystemException) { org.junit.Assume.assumeNoException(e) }
            runCatching { gate.verify { throw IllegalStateException("unverified") } }
            gate.ifAdmitted { log.appendText("should never be written") }
            assertEquals("unchanged", target.readText())
        } finally { directory.deleteRecursively() }
    }
    @Test fun framebufferWorkRequiresCurrentAdmission() {
        val gate = AdmissionState()
        var reads = 0
        gate.ifAdmitted { reads++ }
        assertEquals(0, reads)
        gate.verify { }
        gate.ifAdmitted { reads++ }
        assertEquals(1, reads)
        gate.reset()
        gate.ifAdmitted { reads++ }
        assertEquals(1, reads)
        gate.verify { }
        runCatching { gate.verify { throw IllegalStateException("unverified") } }
        gate.ifAdmitted { reads++ }
        assertEquals(1, reads)
    }
    @Test fun failedVerificationDoesNotAllowGuestCleanup() {
        val gate = AdmissionState()
        runCatching { gate.verify { throw IllegalStateException("unverified") } }
        assertFalse(gate.admitted)
    }
    @Test fun subsequentFailureClearsPriorAdmission() {
        val gate = AdmissionState()
        gate.verify { }
        assertTrue(gate.admitted)
        runCatching { gate.verify { throw IllegalStateException("unverified") } }
        assertFalse(gate.admitted)
    }
}
