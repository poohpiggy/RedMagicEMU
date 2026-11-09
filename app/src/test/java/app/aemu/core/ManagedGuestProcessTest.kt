package app.aemu.core

import org.junit.Assert.*
import org.junit.Test
import java.io.InputStream
import java.io.OutputStream
import java.util.concurrent.TimeUnit
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors

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

    @Test fun borrowedHandleIsPinnedUntilNonblockingOperationReturns() {
        val child = Child()
        val closed = mutableListOf<Int>()
        val managed = ManagedGuestProcess(child, 123, 42, { _, _ -> 0 }, { closed += it })
        val entered = CountDownLatch(1)
        val release = CountDownLatch(1)
        val pool = Executors.newFixedThreadPool(2)
        try {
            val borrow = pool.submit<Int> {
                managed.withAuthenticatedHandle { pid, fd ->
                    assertEquals(123, pid); assertEquals(42, fd)
                    entered.countDown()
                    assertTrue(release.await(2, TimeUnit.SECONDS))
                    17
                }
            }
            assertTrue(entered.await(2, TimeUnit.SECONDS))
            val waiter = pool.submit<Int> { managed.waitFor() }
            try { waiter.get(100, TimeUnit.MILLISECONDS); fail("closed borrowed handle") }
            catch (_: java.util.concurrent.TimeoutException) { }
            assertTrue(closed.isEmpty())
            release.countDown()
            assertEquals(17, borrow.get(2, TimeUnit.SECONDS).toInt())
            assertEquals(37, waiter.get(2, TimeUnit.SECONDS).toInt())
            assertEquals(listOf(42), closed)
            try { managed.withAuthenticatedHandle { _, _ -> fail("closed fd borrowed") }; fail("accepted exited process") }
            catch (_: IllegalStateException) { }
        } finally { release.countDown(); pool.shutdownNow() }
    }

    @Test fun borrowExceptionReleasesLockAndDoesNotCloseLiveHandle() {
        val child = Child()
        val closed = mutableListOf<Int>()
        val managed = ManagedGuestProcess(child, 123, 42, { _, _ -> 0 }, { closed += it })
        try { managed.withAuthenticatedHandle<Unit> { _, _ -> throw IllegalArgumentException("failed") }; fail() }
        catch (_: IllegalArgumentException) { }
        assertTrue(closed.isEmpty())
        assertTrue(managed.withAuthenticatedHandle { pid, fd ->
            assertEquals(123, pid); assertEquals(42, fd)
            true
        })
        managed.waitFor()
        assertEquals(listOf(42), closed)
    }
}
