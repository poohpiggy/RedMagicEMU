package app.aemu.core

import org.junit.Assert.*
import org.junit.Test
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.IOException
import java.io.InputStream
import java.io.OutputStream
import java.io.File
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import kotlin.concurrent.thread

class BoundedGuestCommandTest {
    private class TestProcess(
        val source: InputStream,
        private val waits: ArrayDeque<Boolean> = ArrayDeque(listOf(true)),
        private val code: Int = 0,
        private val throwOnWait: Boolean = false,
        private val failWait: Boolean = false,
    ) : Process() {
        var exitReads = 0
        var killed = false
        override fun getInputStream(): InputStream = source
        override fun getErrorStream(): InputStream = ByteArrayInputStream(byteArrayOf())
        override fun getOutputStream(): OutputStream = ByteArrayOutputStream()
        override fun waitFor(): Int = code
        override fun waitFor(timeout: Long, unit: TimeUnit): Boolean {
            if (throwOnWait) throw InterruptedException("test")
            if (failWait) throw IllegalStateException("test")
            return waits.removeFirstOrNull() ?: false
        }
        override fun exitValue(): Int { exitReads++; return code }
        override fun destroy() { killed = true }
        override fun destroyForcibly(): Process { killed = true; return this }
    }

    private fun executor(limit: Int = 65_536) = BoundedGuestCommand(
        outputLimitBytes = limit, readerWaitMs = 50, terminateWaitMs = 50,
    )

    @Test fun zeroExitWithCompleteOutputIsReportedExactly() {
        val p = TestProcess(ByteArrayInputStream("Result: Bundle[{}]\n".toByteArray()))
        assertEquals(BoundedGuestCommand.Result.Completed(0, "Result: Bundle[{}]\n"),
            executor().run(100) { p })
        assertEquals(1, p.exitReads)
    }

    @Test fun realJvmProcessCompletesOnlyAfterMergedOutputEof() {
        val executable = if (System.getProperty("os.name").startsWith("Windows")) "java.exe" else "java"
        val java = File(System.getProperty("java.home"), "bin/$executable")
        val result = BoundedGuestCommand().run(20_000) { ProcessBuilder(java.absolutePath, "-version").redirectErrorStream(true).start() }
        assertTrue(result is BoundedGuestCommand.Result.Completed)
        result as BoundedGuestCommand.Result.Completed
        assertEquals(0, result.exitCode)
        assertTrue(result.output.contains("version", ignoreCase = true))
    }

    @Test fun timeoutNeverBecomesSuccessWhenExitValueWouldBeZero() {
        val p = TestProcess(ByteArrayInputStream("Result: Bundle[{}]\n".toByteArray()),
            ArrayDeque(listOf(false, true)))
        val result = executor().run(100) { p }
        assertTrue(result is BoundedGuestCommand.Result.Uncertain)
        result as BoundedGuestCommand.Result.Uncertain
        assertEquals(BoundedGuestCommand.Reason.TIMEOUT, result.reason)
        assertTrue(result.terminationRequested)
        assertTrue(p.killed)
        assertEquals("do not inspect a later zero exit after timeout", 0, p.exitReads)
    }

    @Test fun zeroExitWithoutObservedEofDoesNotSucceed() {
        val readStarted = CountDownLatch(1)
        val release = CountDownLatch(1)
        val stream = object : InputStream() {
            override fun read(): Int { readStarted.countDown(); release.await(); return -1 }
        }
        val p = TestProcess(stream)
        val result: BoundedGuestCommand.Result
        try {
            result = executor().run(100) { p }
            assertTrue(readStarted.await(1, TimeUnit.SECONDS))
            assertTrue(result is BoundedGuestCommand.Result.Uncertain)
            result as BoundedGuestCommand.Result.Uncertain
            assertEquals(BoundedGuestCommand.Reason.READER_PENDING, result.reason)
            assertFalse(result.readerFinished)
            assertFalse(result.eofObserved)
        } finally { release.countDown() }
    }

    @Test fun timeoutWithBlockedReaderReportsBothUnconfirmed() {
        val entered = CountDownLatch(1)
        val release = CountDownLatch(1)
        val stream = object : InputStream() {
            override fun read(): Int { entered.countDown(); release.await(); return -1 }
        }
        val process = TestProcess(stream, ArrayDeque(listOf(false, false)))
        try {
            val result = executor().run(100) { process } as BoundedGuestCommand.Result.Uncertain
            assertTrue(entered.await(1, TimeUnit.SECONDS))
            assertEquals(BoundedGuestCommand.Reason.TIMEOUT, result.reason)
            assertFalse(result.processExitObserved)
            assertFalse(result.readerFinished)
            assertFalse(result.eofObserved)
            assertTrue(result.terminationRequested)
        } finally { release.countDown() }
    }

    @Test fun readingFailureIsNotReportedAsSuccess() {
        val stream = object : InputStream() { override fun read(): Int = throw IOException("test") }
        val result = executor().run(100) { TestProcess(stream) }
        assertTrue(result is BoundedGuestCommand.Result.Uncertain)
        result as BoundedGuestCommand.Result.Uncertain
        assertEquals(BoundedGuestCommand.Reason.READ_FAILURE, result.reason)
        assertTrue(result.readerFinished)
        assertFalse(result.eofObserved)
    }

    @Test fun truncatedOutputCannotSucceedEvenWhenItStartsWithExpectedText() {
        val stream = ByteArrayInputStream("Result: Bundle[{}]\nextra".toByteArray())
        val result = executor(limit = 18).run(100) { TestProcess(stream) }
        assertTrue(result is BoundedGuestCommand.Result.Uncertain)
        result as BoundedGuestCommand.Result.Uncertain
        assertEquals(BoundedGuestCommand.Reason.OUTPUT_TRUNCATED, result.reason)
        assertTrue(result.readerFinished)
        assertTrue(result.eofObserved)
    }

    @Test fun invalidUtf8IsUncertainEvenAfterEof() {
        val result = executor().run(100) { TestProcess(ByteArrayInputStream(byteArrayOf(0xc3.toByte(), 0x28))) }
        assertTrue(result is BoundedGuestCommand.Result.Uncertain)
        assertEquals(BoundedGuestCommand.Reason.INVALID_UTF8, (result as BoundedGuestCommand.Result.Uncertain).reason)
    }

    @Test fun streamCloseFailureRemainsUncertainEvenAfterEof() {
        val stream = object : ByteArrayInputStream(byteArrayOf()) {
            override fun close() { throw IOException("test") }
        }
        val result = executor().run(100) { TestProcess(stream) } as BoundedGuestCommand.Result.Uncertain
        assertEquals(BoundedGuestCommand.Reason.READ_FAILURE, result.reason)
        assertTrue(result.readerFinished)
        assertTrue(result.eofObserved)
    }

    @Test fun interruptedWaitRestoresInterruptAndDoesNotClaimCompletion() {
        var result: BoundedGuestCommand.Result? = null
        var interrupted = false
        val process = TestProcess(ByteArrayInputStream(byteArrayOf()), throwOnWait = true)
        val worker = thread {
            result = executor().run(100) { process }
            interrupted = Thread.currentThread().isInterrupted
        }
        worker.join(2_000)
        assertFalse(worker.isAlive)
        assertTrue(interrupted)
        assertTrue(process.killed)
        assertEquals(BoundedGuestCommand.Reason.INTERRUPTED,
            (result as BoundedGuestCommand.Result.Uncertain).reason)
        assertEquals(0, process.exitReads)
    }

    @Test fun startFailureDoesNotPretendAProcessExited() {
        val result = executor().run(100) { throw IOException("test") }
        assertTrue(result is BoundedGuestCommand.Result.Uncertain)
        result as BoundedGuestCommand.Result.Uncertain
        assertEquals(BoundedGuestCommand.Reason.START_FAILURE, result.reason)
        assertFalse(result.processExitObserved)
    }

    @Test fun interruptedStartRestoresInterruptAndDoesNotClaimCompletion() {
        var result: BoundedGuestCommand.Result? = null
        var interrupted = false
        val worker = thread {
            result = executor().run(100) { throw InterruptedException("test") }
            interrupted = Thread.currentThread().isInterrupted
        }
        worker.join(2_000)
        assertFalse(worker.isAlive)
        assertTrue(interrupted)
        assertEquals(BoundedGuestCommand.Reason.INTERRUPTED,
            (result as BoundedGuestCommand.Result.Uncertain).reason)
        assertFalse((result as BoundedGuestCommand.Result.Uncertain).processExitObserved)
    }

    @Test fun unexpectedWaitFailureRequestsTerminationWithoutClaimingExit() {
        val process = TestProcess(ByteArrayInputStream(byteArrayOf()), failWait = true)
        val result = executor().run(100) { process } as BoundedGuestCommand.Result.Uncertain
        assertEquals(BoundedGuestCommand.Reason.WAIT_FAILURE, result.reason)
        assertTrue(result.terminationRequested)
        assertTrue(process.killed)
        assertFalse(result.processExitObserved)
        assertEquals(0, process.exitReads)
    }
}
