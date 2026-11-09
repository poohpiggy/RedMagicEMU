package app.aemu.core

import org.junit.Assert.*
import org.junit.Test
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import kotlin.concurrent.thread

class SupervisedGuestProcessTest {
    private fun create(signals: MutableList<Int> = mutableListOf()) = SupervisedGuestProcess(
        ByteArrayInputStream("out".toByteArray()), ByteArrayInputStream("err".toByteArray()),
        ByteArrayOutputStream(), { signals.add(it) },
    )

    @Test fun realStreamsAndExitFrameDriveProcessContract() {
        val p = create()
        assertEquals("out", p.inputStream.bufferedReader().readText())
        assertEquals("err", p.errorStream.bufferedReader().readText())
        assertTrue(p.isAlive)
        try { p.exitValue(); fail("running process has an exit code") } catch (_: IllegalThreadStateException) { }
        assertFalse(p.waitFor(10, TimeUnit.MILLISECONDS))
        p.onExitFrame(37)
        assertTrue(p.waitFor(1, TimeUnit.SECONDS))
        assertEquals(37, p.waitFor())
        assertEquals(37, p.exitValue())
        assertFalse(p.isAlive)
    }

    @Test fun waitingThreadWakesOnExit() {
        val p = create(); val done = CountDownLatch(1); var code = -1
        val worker = thread { code = p.waitFor(); done.countDown() }
        p.onExitFrame(23)
        assertTrue(done.await(1, TimeUnit.SECONDS)); worker.join()
        assertEquals(23, code)
    }

    @Test fun disconnectReportsUnknownInsteadOfExit() {
        val p = create()
        p.onControlLost()
        assertTrue("unknown process must not be reported as exited", p.isAlive)
        try { p.waitFor(); fail("unknown exit accepted") } catch (_: IllegalStateException) { }
        try { p.exitValue(); fail("unknown exit code accepted") } catch (_: IllegalStateException) { }
        try { p.destroy(); fail("unknown session signalled") } catch (_: IllegalStateException) { }
    }

    @Test fun disconnectWakesWaiterWithoutInventingExitCode() {
        val p = create(); val entered = CountDownLatch(1); val done = CountDownLatch(1)
        var unknown = false
        val worker = thread {
            entered.countDown()
            try { p.waitFor() } catch (_: IllegalStateException) { unknown = true }
            finally { done.countDown() }
        }
        assertTrue(entered.await(1, TimeUnit.SECONDS)); p.onControlLost()
        assertTrue(done.await(1, TimeUnit.SECONDS)); worker.join()
        assertTrue(unknown); assertTrue(p.isAlive)
    }

    @Test fun signalFailureIsNotReportedAsSuccessfulTermination() {
        val p = SupervisedGuestProcess(ByteArrayInputStream(byteArrayOf()), ByteArrayInputStream(byteArrayOf()),
            ByteArrayOutputStream(), { throw java.io.IOException("signal failed") })
        try { p.destroyForcibly(); fail("signal failure suppressed") } catch (_: java.io.IOException) { }
        assertTrue(p.isAlive)
    }

    @Test fun blockedSignalDoesNotBlockExitFrame() {
        val entered = CountDownLatch(1); val release = CountDownLatch(1); val delivered = CountDownLatch(1)
        val p = SupervisedGuestProcess(ByteArrayInputStream(byteArrayOf()), ByteArrayInputStream(byteArrayOf()),
            ByteArrayOutputStream(), { entered.countDown(); release.await() })
        val signal = thread { p.destroy() }
        assertTrue(entered.await(1, TimeUnit.SECONDS))
        val exit = thread { p.onExitFrame(0); delivered.countDown() }
        try { assertTrue("exit frame blocked by signal I/O", delivered.await(300, TimeUnit.MILLISECONDS)) }
        finally { release.countDown(); signal.join(1000); exit.join(1000) }
        assertEquals(0, p.exitValue())
    }

    @Test fun signalIsSessionBoundAndNotSentAfterExit() {
        val signals = mutableListOf<Int>(); val p = create(signals)
        p.destroy(); assertSame(p, p.destroyForcibly()); assertEquals(listOf(15, 9), signals)
        p.onExitFrame(0); p.destroy(); p.destroyForcibly()
        assertEquals(listOf(15, 9), signals)
    }

    @Test fun duplicateInconsistentExitIsAProtocolFailure() {
        val p = create(); p.onExitFrame(0); p.onExitFrame(0)
        try { p.onExitFrame(1); fail("conflicting exit frame accepted") } catch (_: IllegalStateException) { }
        assertEquals(0, p.exitValue())
    }
}
