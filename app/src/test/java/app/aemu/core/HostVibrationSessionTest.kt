package app.aemu.core

import org.junit.Assert.*
import org.junit.Test
import java.util.Collections
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.atomic.AtomicReference
import java.io.IOException

class HostVibrationSessionTest {
    private class Input(private val next: () -> Int, private val onClose: () -> Unit) : HostVibrationInput, AutoCloseable {
        override fun read(sequence: Long): Int = next()
        override fun close() = onClose()
    }

    private fun start(control: HostVibrationControl, input: Input): AutoCloseable {
        return HostVibrationSession(control, input)
    }

    private fun awaitWaiting(thread: Thread) {
        val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(5)
        while (thread.isAlive && thread.state != Thread.State.WAITING && System.nanoTime() < deadline) {
            Thread.sleep(5)
        }
        assertEquals(Thread.State.WAITING, thread.state)
    }

    @Test fun explicitCloseStopsAndReleasesInputOnce() {
        val started = CountDownLatch(1)
        val events = Collections.synchronizedList(mutableListOf<String>())
        val input = Input({ started.countDown(); 0 }, { events.add("close-input") })
        val session = start(HostVibrationControl({ events.add("pulse") }, { events.add("cancel") }), input)
        assertTrue(started.await(5, TimeUnit.SECONDS))
        session.close()
        session.close()
        assertEquals(listOf("cancel", "close-input"), events)
    }

    @Test fun fakeInputDisconnectErrorStopsAndReleasesWithoutAnotherRequest() {
        val released = CountDownLatch(1)
        val events = Collections.synchronizedList(mutableListOf<String>())
        val count = AtomicInteger()
        val input = Input({ if (count.getAndIncrement() == 0) 501 else -1 }, {
            events.add("close-input"); released.countDown()
        })
        val session = start(HostVibrationControl({ events.add("pulse:$it") }, { events.add("cancel") }), input)
        assertTrue(released.await(5, TimeUnit.SECONDS))
        assertEquals(listOf("pulse:500", "cancel", "close-input"), events)
        val failure = assertThrows(IllegalStateException::class.java) { session.close() }
        assertSame(failure, assertThrows(IllegalStateException::class.java) { session.close() })
    }

    @Test fun cancellationFailureStillReleasesInputAndRemainsVisible() {
        val started = CountDownLatch(1)
        val closed = AtomicInteger()
        val input = Input({ started.countDown(); 0 }, { closed.incrementAndGet() })
        val session = start(HostVibrationControl({}, { error("cancel failed") }), input)
        assertTrue(started.await(5, TimeUnit.SECONDS))
        val failure = assertThrows(IllegalStateException::class.java) { session.close() }
        assertEquals("cancel failed", failure.message)
        assertEquals(1, closed.get())
        assertSame(failure, assertThrows(IllegalStateException::class.java) { session.close() })
    }

    @Test fun failedSessionStartReleasesOwnedInput() {
        val closed = AtomicInteger()
        val input = Input({ 0 }, { closed.incrementAndGet() })
        val control = HostVibrationControl({}, {})
        control.start()
        assertThrows(IllegalStateException::class.java) { start(control, input) }
        assertEquals(1, closed.get())
    }

    @Test fun closeDropsInFlightReadBeforeCancellingAndReleasing() {
        val entered = CountDownLatch(1)
        val release = CountDownLatch(1)
        val completed = CountDownLatch(1)
        val events = Collections.synchronizedList(mutableListOf<String>())
        val error = AtomicReference<Throwable?>()
        val input = Input({
            entered.countDown()
            check(release.await(5, TimeUnit.SECONDS))
            501
        }, { events.add("close-input") })
        val session = start(HostVibrationControl({ events.add("pulse:$it") }, { events.add("cancel") }), input)
        assertTrue(entered.await(5, TimeUnit.SECONDS))
        val closer = Thread {
            try { session.close(); completed.countDown() } catch (failure: Throwable) { error.set(failure) }
        }
        closer.start()
        try {
            awaitWaiting(closer)
            assertEquals(1L, completed.count)
        }
        finally { release.countDown(); closer.join(5000) }
        assertFalse(closer.isAlive)
        assertNull(error.get())
        assertEquals(0, completed.count)
        assertEquals(listOf("cancel", "close-input"), events)
    }

    @Test fun transportAndBothCleanupFailuresRemainVisibleWithoutDuplicates() {
        val released = CountDownLatch(1)
        val input = Input({ throw IOException("read failure") }, {
            released.countDown()
            throw IOException("input close failure")
        })
        val session = start(HostVibrationControl({}, { error("cancel failure") }), input)
        assertTrue(released.await(5, TimeUnit.SECONDS))
        val failure = assertThrows(IOException::class.java) { session.close() }
        assertEquals("read failure", failure.message)
        assertEquals(listOf("cancel failure", "input close failure"), failure.suppressed.map { it.message })
        assertSame(failure, assertThrows(IOException::class.java) { session.close() })
    }

    @Test fun interruptedCloseWaitsForCleanupThenReportsAndRestoresInterruption() {
        val entered = CountDownLatch(1)
        val release = CountDownLatch(1)
        val finished = CountDownLatch(1)
        val closed = AtomicInteger()
        val failure = AtomicReference<Throwable?>()
        val interrupted = AtomicReference<Boolean>()
        val input = Input({
            entered.countDown()
            check(release.await(5, TimeUnit.SECONDS))
            0
        }, { closed.incrementAndGet() })
        val session = start(HostVibrationControl({}, {}), input)
        assertTrue(entered.await(5, TimeUnit.SECONDS))
        val closer = Thread {
            try { session.close() }
            catch (error: Throwable) { failure.set(error); interrupted.set(Thread.currentThread().isInterrupted) }
            finally { finished.countDown() }
        }
        closer.start()
        try {
            awaitWaiting(closer)
            closer.interrupt()
            assertFalse(finished.await(200, TimeUnit.MILLISECONDS))
        } finally { release.countDown(); closer.join(5000) }
        assertFalse(closer.isAlive)
        assertTrue(failure.get() is InterruptedException)
        assertEquals(true, interrupted.get())
        assertEquals(1, closed.get())
        session.close()
    }
}
