package app.aemu.core

import org.junit.Assert.*
import org.junit.Test
import java.io.IOException
import java.util.Collections
import java.util.concurrent.CountDownLatch
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference

class HostVibrationBinderBridgeTest {
    private class FakeTransport : HostVibrationBinderTransport {
        private val events = LinkedBlockingQueue<HostVibrationBinderEvent>()
        val completed = Collections.synchronizedList(mutableListOf<Pair<Long, Boolean>>())
        val closed = CountDownLatch(1)
        val awaiting = CountDownLatch(1)
        @Volatile var stopped = false

        fun offer(token: Long, code: Int, duration: Long = 0) {
            events.put(HostVibrationBinderEvent(token, code, duration))
        }
        override fun next(): HostVibrationBinderEvent? {
            awaiting.countDown()
            val event = events.poll(5, TimeUnit.SECONDS) ?: throw IOException("timed out")
            return if (event.token == 0L) null else event
        }
        override fun complete(token: Long, success: Boolean) { completed.add(token to success) }
        override fun requestStop() { stopped = true; events.offer(HostVibrationBinderEvent(0, 0, 0)) }
        override fun close() { closed.countDown() }
    }

    @Test fun forwardsRequestsAndRetiresHostVibratorOnClose() {
        val actions = Collections.synchronizedList(mutableListOf<String>())
        val transport = FakeTransport()
        transport.offer(1, 1)
        transport.offer(2, 3, 500)
        transport.offer(3, 2)
        val bridge = HostVibrationBinderBridge(HostVibrationControl({ actions.add("pulse:$it") }, { actions.add("cancel") }), transport)
        val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(5)
        while (transport.completed.size < 3 && System.nanoTime() < deadline) Thread.yield()
        assertEquals(listOf(1L to true, 2L to true, 3L to true), transport.completed.toList())
        bridge.close()
        bridge.close()
        assertEquals(listOf("pulse:500", "cancel", "cancel"), actions.toList())
        assertEquals(0, transport.closed.count)
    }

    @Test fun hostFailureIsReportedAndStillCancelsAndCloses() {
        val actions = Collections.synchronizedList(mutableListOf<String>())
        val transport = FakeTransport().apply { offer(1, 3, 500) }
        val failure = IOException("pulse failed")
        val bridge = HostVibrationBinderBridge(HostVibrationControl({ throw failure }, { actions.add("cancel") }), transport)
        assertTrue(transport.closed.await(5, TimeUnit.SECONDS))
        assertSame(failure, assertThrows(IOException::class.java) { bridge.close() })
        assertEquals(listOf(1L to false), transport.completed.toList())
        assertEquals(listOf("cancel"), actions.toList())
    }

    @Test fun closeWaitsForInFlightPulseThenCancelsBeforeReturning() {
        val entered = CountDownLatch(1)
        val release = CountDownLatch(1)
        val actions = Collections.synchronizedList(mutableListOf<String>())
        val transport = FakeTransport().apply { offer(1, 3, 500) }
        val bridge = HostVibrationBinderBridge(HostVibrationControl({
            entered.countDown()
            check(release.await(5, TimeUnit.SECONDS))
            actions.add("pulse")
        }, { actions.add("cancel") }), transport)
        assertTrue(entered.await(5, TimeUnit.SECONDS))
        val failure = AtomicReference<Throwable?>()
        val closer = Thread { try { bridge.close() } catch (error: Throwable) { failure.set(error) } }
        closer.start()
        try { assertTrue(closer.isAlive); assertFalse(transport.stopped) }
        finally { release.countDown(); closer.join(5000) }
        assertFalse(closer.isAlive)
        assertNull(failure.get())
        assertEquals(listOf("pulse", "cancel"), actions.toList())
        assertTrue(transport.stopped)
    }

    @Test fun invalidEventIsNeverAcknowledgedAsSuccess() {
        val transport = FakeTransport().apply { offer(1, 3, 5001) }
        val actions = Collections.synchronizedList(mutableListOf<String>())
        val bridge = HostVibrationBinderBridge(HostVibrationControl({ actions.add("pulse") }, { actions.add("cancel") }), transport)
        assertTrue(transport.closed.await(5, TimeUnit.SECONDS))
        assertThrows(IllegalStateException::class.java) { bridge.close() }
        assertEquals(listOf(1L to false), transport.completed.toList())
        assertEquals(listOf("cancel"), actions.toList())
    }
}
