package app.aemu.core

import org.junit.Assert.*
import org.junit.Test
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

class SessionInputGateTest {
    @Test fun closedSessionDoesNotCaptureOrDispatchInput() {
        val gate = SessionInputGate()
        val session = gate.begin(blocked = true)
        assertNull(gate.capture())
        var sent = false
        assertFalse(gate.dispatch(gate.capture()) { sent = true })
        assertFalse(sent)
        assertTrue(gate.open(session))
        assertNotNull(gate.capture())
        assertTrue(gate.dispatch(gate.capture()) { sent = true })
        assertTrue(sent)
    }

    @Test fun operationCreatedDuringBlockCannotBecomeValidAfterOpen() {
        val gate = SessionInputGate()
        val session = gate.begin(blocked = true)
        val queued = gate.capture()
        assertTrue(gate.open(session))
        var sent = false
        assertFalse(gate.dispatch(queued) { sent = true })
        assertFalse(sent)
    }

    @Test fun oldSessionCannotDispatchOpenOrStopNewSession() {
        val gate = SessionInputGate()
        val first = gate.begin(blocked = false)
        val queued = gate.capture()
        val second = gate.begin(blocked = true)
        assertFalse(gate.dispatch(queued) { fail("stale operation delivered") })
        assertFalse(gate.open(first))
        assertFalse(gate.stop(first))
        assertNull(gate.capture())
        assertTrue(gate.open(second))
        assertTrue(gate.dispatch(gate.capture()) {})
    }

    @Test fun stopInvalidatesAlreadyCapturedOperations() {
        val gate = SessionInputGate()
        val session = gate.begin(blocked = false)
        val queued = gate.capture()
        assertTrue(gate.stop(session))
        assertNull(gate.capture())
        assertFalse(gate.dispatch(queued) { fail("stopped operation delivered") })
        assertFalse(gate.open(session))
    }

    @Test fun dispatchRejectsReentrantSessionLifecycleChanges() {
        val gate = SessionInputGate()
        val session = gate.begin(blocked = false)
        val permit = gate.capture()
        assertTrue(gate.dispatch(permit) {
            assertThrows(IllegalStateException::class.java) { gate.begin(blocked = true) }
            assertThrows(IllegalStateException::class.java) { gate.stop(session) }
            assertThrows(IllegalStateException::class.java) { gate.open(session) }
            assertTrue(gate.dispatch(permit) {
                assertThrows(IllegalStateException::class.java) { gate.begin(blocked = true) }
            })
            assertThrows(IllegalStateException::class.java) { gate.stop(session) }
        })
        assertTrue(gate.stop(session))
        assertFalse(gate.dispatch(permit) { fail("stopped operation delivered") })
    }

    @Test fun failedActionDoesNotLeaveLifecycleBlocked() {
        val gate = SessionInputGate()
        val session = gate.begin(blocked = false)
        val permit = gate.capture()
        assertThrows(IllegalArgumentException::class.java) {
            gate.dispatch(permit) { throw IllegalArgumentException("test") }
        }
        assertTrue(gate.stop(session))
    }

    @Test fun replacingSessionWaitsUntilInFlightDispatchFinishes() {
        val gate = SessionInputGate()
        gate.begin(blocked = false)
        val permit = gate.capture()
        val entered = CountDownLatch(1)
        val release = CountDownLatch(1)
        val beginning = CountDownLatch(1)
        val replaced = CountDownLatch(1)
        val worker = Thread {
            gate.dispatch(permit) { entered.countDown(); check(release.await(5, TimeUnit.SECONDS)) }
        }
        worker.start()
        assertTrue(entered.await(5, TimeUnit.SECONDS))
        val replacer = Thread {
            beginning.countDown()
            gate.begin(blocked = true)
            replaced.countDown()
        }
        try {
            replacer.start()
            assertTrue(beginning.await(5, TimeUnit.SECONDS))
            assertFalse(replaced.await(200, TimeUnit.MILLISECONDS))
        } finally {
            release.countDown()
            worker.join(5000)
            replacer.join(5000)
        }
        assertFalse(worker.isAlive)
        assertFalse(replacer.isAlive)
        assertEquals(0L, replaced.count)
        assertFalse(gate.dispatch(permit) { fail("stale operation delivered") })
    }
}
