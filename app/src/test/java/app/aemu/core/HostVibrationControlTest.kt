package app.aemu.core

import org.junit.Assert.*
import org.junit.Test
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference

class HostVibrationControlTest {
    @Test fun activeSessionPulsesTheRequestedDuration() {
        val pulses = mutableListOf<Long>()
        val control = HostVibrationControl({ pulses.add(it) }) {}
        val session = control.start()
        assertTrue(control.vibrate(session, 500))
        assertEquals(listOf(500L), pulses)
    }

    @Test fun outOfRangeAndWrongSessionNeverReachTheMotor() {
        val pulses = mutableListOf<Long>()
        var cancels = 0
        val control = HostVibrationControl({ pulses.add(it) }) { cancels++ }
        val session = control.start()
        assertFalse(control.vibrate(session, -1))
        assertFalse(control.vibrate(session, 0))
        assertFalse(control.vibrate(session, HostVibrationControl.MAX_PULSE_MS + 1))
        assertFalse(control.vibrate(HostVibrationControl.Session(), 500))
        assertFalse(control.cancel(HostVibrationControl.Session()))
        assertFalse(control.stop(HostVibrationControl.Session()))
        assertTrue(control.vibrate(session, HostVibrationControl.MAX_PULSE_MS))
        assertEquals(listOf(HostVibrationControl.MAX_PULSE_MS), pulses)
        assertEquals(0, cancels)
    }

    @Test fun cancelAndStopOperateOnlyOnTheActiveSession() {
        val pulses = mutableListOf<Long>()
        var cancels = 0
        val control = HostVibrationControl({ pulses.add(it) }) { cancels++ }
        val session = control.start()
        assertTrue(control.vibrate(session, 500))
        assertTrue(control.cancel(session))
        assertTrue(control.stop(session))
        assertEquals(2, cancels)
        assertFalse(control.vibrate(session, 500))
        assertFalse(control.cancel(session))
        assertFalse(control.stop(session))
        assertEquals(2, cancels)
        assertEquals(listOf(500L), pulses)
        assertThrows(IllegalStateException::class.java) { control.start() }
    }

    @Test fun stopWaitsForTheCurrentPulseAndPreventsLaterPulses() {
        val entered = CountDownLatch(1)
        val release = CountDownLatch(1)
        val stopStarted = CountDownLatch(1)
        val stopped = CountDownLatch(1)
        val failure = AtomicReference<Throwable?>()
        val events = mutableListOf<String>()
        val control = HostVibrationControl({
            events.add("pulse")
            entered.countDown()
            check(release.await(5, TimeUnit.SECONDS))
        }) { events.add("cancel") }
        val session = control.start()
        val worker = Thread {
            try { assertTrue(control.vibrate(session, 500)) }
            catch (t: Throwable) { failure.compareAndSet(null, t) }
        }
        val stopper = Thread {
            try {
                stopStarted.countDown()
                assertTrue(control.stop(session))
                stopped.countDown()
            } catch (t: Throwable) { failure.compareAndSet(null, t) }
        }
        worker.start()
        try {
            assertTrue(entered.await(5, TimeUnit.SECONDS))
            stopper.start()
            assertTrue(stopStarted.await(5, TimeUnit.SECONDS))
            assertFalse(stopped.await(200, TimeUnit.MILLISECONDS))
        } finally {
            release.countDown()
            worker.join(5000)
            if (stopper.state != Thread.State.NEW) stopper.join(5000)
        }
        assertFalse(worker.isAlive)
        assertFalse(stopper.isAlive)
        assertNull(failure.get())
        assertEquals(0L, stopped.count)
        assertFalse(control.vibrate(session, 500))
        assertEquals(listOf("pulse", "cancel"), events)
    }

    @Test fun sinkFailuresArePropagatedAndFailedStopStillRetiresSession() {
        val control = HostVibrationControl({ throw IllegalStateException("pulse failed") }) {
            throw IllegalStateException("cancel failed")
        }
        val session = control.start()
        assertEquals("pulse failed", assertThrows(IllegalStateException::class.java) {
            control.vibrate(session, 500)
        }.message)
        assertEquals("cancel failed", assertThrows(IllegalStateException::class.java) {
            control.stop(session)
        }.message)
        assertFalse(control.vibrate(session, 500))
        assertFalse(control.stop(session))
    }

    @Test fun reentrantSinkCannotStopSessionMidPulse() {
        lateinit var control: HostVibrationControl
        lateinit var session: HostVibrationControl.Session
        var cancels = 0
        control = HostVibrationControl({
            assertThrows(IllegalStateException::class.java) { control.stop(session) }
        }) { cancels++ }
        session = control.start()
        assertTrue(control.vibrate(session, 1))
        assertEquals(0, cancels)
        assertTrue(control.stop(session))
        assertEquals(1, cancels)
    }
}
