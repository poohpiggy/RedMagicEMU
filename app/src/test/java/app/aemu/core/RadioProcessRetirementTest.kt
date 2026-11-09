package app.aemu.core

import java.io.Closeable
import java.io.File
import java.io.InputStream
import java.io.OutputStream
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger
import kotlin.concurrent.thread
import org.junit.Assert.*
import org.junit.Test

class RadioProcessRetirementTest {
    @Test fun failedCloseRemainsForStopRetry() {
        val original = java.io.IOException("close failed")
        val closed = RadioChannelCloseOnce()
        var attempts = 0
        val channel = Closeable { closed.close { attempts++; throw original } }
        var current: Closeable? = channel
        val first = closeRadioChannelForStop(current) { current = null }
        assertSame(original, first)
        assertSame("failed close discarded the channel", channel, current)
        val second = closeRadioChannelForStop(current) { current = null }
        assertSame("retry lost previous failure", original, second)
        assertSame(channel, current)
        assertEquals("failed close was repeated", 1, attempts)
    }

    @Test fun successfulCloseClearsChannel() {
        var current: Closeable? = Closeable { }
        assertNull(closeRadioChannelForStop(current) { current = null })
        assertNull(current)
    }
    private class Child : Process() {
        val exited = CountDownLatch(1)
        override fun waitFor(): Int { exited.await(); return 17 }
        override fun waitFor(timeout: Long, unit: TimeUnit): Boolean = exited.await(timeout, unit)
        override fun exitValue(): Int = if (exited.count == 0L) 17 else throw IllegalThreadStateException()
        override fun isAlive(): Boolean = exited.count != 0L
        override fun destroy() { exited.countDown() }
        override fun getInputStream(): InputStream = InputStream.nullInputStream()
        override fun getErrorStream(): InputStream = InputStream.nullInputStream()
        override fun getOutputStream(): OutputStream = OutputStream.nullOutputStream()
    }

    @Test fun exitingRadioRetiresItsOwnConnectionWithoutTouchingReplacement() {
        val oldProcess = Child()
        val oldClosed = CountDownLatch(1)
        val newClosed = CountDownLatch(1)
        val oldChannel = Closeable { oldClosed.countDown() }
        val newChannel = Closeable { newClosed.countDown() }
        val worker = thread { waitForRadioProcess(oldProcess, oldChannel) }
        try {
            assertFalse("live process lost its connection", oldClosed.await(100, TimeUnit.MILLISECONDS))
            oldProcess.destroy()
            assertTrue("exited process kept its connection", oldClosed.await(3, TimeUnit.SECONDS))
            assertEquals("old process closed replacement connection", 1L, newClosed.count)
        } finally {
            oldProcess.destroy()
            worker.join(3000)
            newChannel.close()
        }
    }

    @Test fun realExitedProcessRetiresConnection() {
        val java = File(System.getProperty("java.home"),
            "bin/java" + if (System.getProperty("os.name").orEmpty().startsWith("Windows")) ".exe" else "")
        val child = ProcessBuilder(java.absolutePath, "-version").redirectErrorStream(true).start()
        val closed = CountDownLatch(1)
        val result = AtomicInteger(-1)
        val worker = thread { result.set(waitForRadioProcess(child, Closeable { closed.countDown() })) }
        try {
            assertTrue("exited process kept its connection", closed.await(5, TimeUnit.SECONDS))
            worker.join(3000)
            assertEquals(0, result.get())
        } finally {
            if (child.isAlive) child.destroyForcibly()
            worker.join(3000)
        }
    }
}
