package app.aemu.core

import org.junit.Assert.*
import org.junit.Test
import java.io.InputStream
import java.io.OutputStream
import java.util.concurrent.TimeUnit

class ManagedGuestProcessTest {
    private class Child : Process() {
        var alive = true
        override fun getOutputStream(): OutputStream = OutputStream.nullOutputStream()
        override fun getInputStream(): InputStream = InputStream.nullInputStream()
        override fun getErrorStream(): InputStream = InputStream.nullInputStream()
        override fun waitFor(): Int { alive = false; return 37 }
        override fun waitFor(timeout: Long, unit: TimeUnit): Boolean = !alive
        override fun exitValue(): Int = if (alive) throw IllegalThreadStateException() else 37
        override fun isAlive(): Boolean = alive
        override fun destroy() = error("must use the pinned process handle")
    }

    @Test fun normalAndForcedStopUseDifferentSignalsAndCloseAfterExit() {
        val child = Child()
        val sent = mutableListOf<Pair<Int, Int>>()
        val closed = mutableListOf<Int>()
        val managed = ManagedGuestProcess(child, 123, 42, { fd, sig -> sent += fd to sig; 0 }, { closed += it })
        managed.destroy()
        managed.destroyForcibly()
        assertEquals(listOf(42 to 15, 42 to 9), sent)
        assertEquals(37, managed.waitFor())
        managed.destroyForcibly()
        assertEquals(listOf(42), closed)
        assertEquals(2, sent.size)
    }

    @Test fun failedSignalDoesNotPretendToStopProcess() {
        val child = Child()
        val managed = ManagedGuestProcess(child, 123, 42, { _, _ -> -1 }, {})
        try {
            managed.destroyForcibly()
            fail("signal error must propagate")
        } catch (expected: IllegalStateException) {
            assertTrue(expected.message!!.contains("errno=1"))
        }
        assertTrue(managed.isAlive)
    }
}
