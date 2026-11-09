package app.aemu.core

import org.junit.Assert.*
import org.junit.Test
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.InputStream
import java.util.concurrent.TimeUnit

class PidfdGuestLauncherPolicyTest {
    @Test fun experimentalGuardSendsCommitThenRequiresLimitedRulesReceipt() {
        val output = ByteArrayOutputStream()
        commitLauncherProtocol(output, ByteArrayInputStream(byteArrayOf('I'.code.toByte())), true)
        assertArrayEquals(byteArrayOf('G'.code.toByte()), output.toByteArray())
    }

    @Test fun experimentalGuardRejectsMissingOrWrongReceipt() {
        for (receipt in listOf(byteArrayOf(), byteArrayOf('X'.code.toByte()))) {
            val output = ByteArrayOutputStream()
            try { commitLauncherProtocol(output, ByteArrayInputStream(receipt), true); fail("accepted missing guard receipt") }
            catch (_: IllegalStateException) { }
            assertArrayEquals(byteArrayOf('G'.code.toByte()), output.toByteArray())
        }
    }

    @Test fun ordinaryLauncherDoesNotWaitForExperimentalReceipt() {
        val output = ByteArrayOutputStream()
        val noRead = object : InputStream() { override fun read(): Int = error("unexpected read") }
        commitLauncherProtocol(output, noRead, false)
        assertArrayEquals(byteArrayOf('G'.code.toByte()), output.toByteArray())
    }

    @Test fun experimentalLauncherCannotFallBackWithoutPidfd() {
        val launcher = PidfdGuestLauncher(File("."), {}, { null }, availabilityOverride = { false })
        val never = ProcessBuilder("redmagicemu-must-not-start-nonexistent")
        try { launcher.startLimitedVibrationExperiment(never, File("/").absoluteFile); fail("experiment fell back") }
        catch (e: IllegalStateException) { assertTrue(e.message!!.contains("authenticated")) }
    }

    @Test fun strictModeDoesNotStartOrdinaryProcessWhenPidfdUnavailable() {
        val launcher = PidfdGuestLauncher(File("."), {}, { null }, availabilityOverride = { false })
        val never = ProcessBuilder("redmagicemu-must-not-start-nonexistent")
        try { launcher.start(never, requireAuthenticated = true); fail("strict launch fell back") }
        catch (e: IllegalStateException) { assertTrue(e.message!!.contains("authenticated")) }
    }

    @Test fun defaultModeRetainsOrdinaryFallback() {
        val launcher = PidfdGuestLauncher(File("."), {}, { null }, availabilityOverride = { false })
        val binary = File(System.getProperty("java.home"), "bin/java" + if (System.getProperty("os.name").orEmpty().startsWith("Windows")) ".exe" else "")
        val process = launcher.start(ProcessBuilder(binary.absolutePath, "-version").redirectErrorStream(true))
        assertTrue(process.waitFor(5, TimeUnit.SECONDS))
        assertEquals(0, process.exitValue())
        assertFalse(process is ManagedGuestProcess)
    }
}
