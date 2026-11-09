package app.aemu.core

import org.junit.Assert.*
import org.junit.Test

class HostVibrationPumpTest {
    @Test fun authenticatedCommandsAreDispatchedInOrderAndCloseCancels() {
        val events = mutableListOf<String>()
        val sequence = mutableListOf<Long>()
        val reads = ArrayDeque(listOf(0, 501, 1))
        val pump = HostVibrationPump(HostVibrationControl({ events.add("on:$it") }, { events.add("off") })) {
            sequence.add(it)
            reads.removeFirst()
        }
        assertFalse(pump.pollOnce())
        assertTrue(pump.pollOnce())
        assertTrue(pump.pollOnce())
        pump.close()
        assertEquals(listOf(1L, 1L, 2L), sequence)
        assertEquals(listOf("on:500", "off", "off"), events)
        assertThrows(IllegalStateException::class.java) { pump.pollOnce() }
    }

    @Test fun rejectedTransportRetiresSessionAndStopsVibration() {
        val events = mutableListOf<String>()
        val reads = ArrayDeque(listOf(501, -1))
        val pump = HostVibrationPump(HostVibrationControl({ events.add("on:$it") }, { events.add("off") })) {
            reads.removeFirst()
        }
        assertTrue(pump.pollOnce())
        assertThrows(IllegalStateException::class.java) { pump.pollOnce() }
        assertEquals(listOf("on:500", "off"), events)
        assertThrows(IllegalStateException::class.java) { pump.pollOnce() }
        pump.close()
        assertEquals(listOf("on:500", "off"), events)
    }

    @Test fun malformedNativeResultCannotReachTheMotor() {
        var pulses = 0
        var cancels = 0
        val pump = HostVibrationPump(HostVibrationControl({ pulses++ }, { cancels++ })) { 5002 }
        assertThrows(IllegalStateException::class.java) { pump.pollOnce() }
        assertEquals(0, pulses)
        assertEquals(1, cancels)
    }

    @Test fun errorsRemainObservableAndCleanupFailureIsRetained() {
        val pump = HostVibrationPump(HostVibrationControl({ error("pulse failure") }, { error("cancel failure") })) { 501 }
        val failure = assertThrows(IllegalStateException::class.java) { pump.pollOnce() }
        assertEquals("pulse failure", failure.message)
        assertEquals("cancel failure", failure.suppressed.single().message)
        assertThrows(IllegalStateException::class.java) { pump.pollOnce() }
        val cleanup = assertThrows(IllegalStateException::class.java) { pump.close() }
        assertEquals("cancel failure", cleanup.message)
        assertSame(cleanup, assertThrows(IllegalStateException::class.java) { pump.close() })
    }

    @Test fun failedCloseRemainsAnObservableFailureOnEveryAttempt() {
        val pump = HostVibrationPump(HostVibrationControl({}, { error("cancel failed") })) { 0 }
        val failure = assertThrows(IllegalStateException::class.java) { pump.close() }
        assertEquals("cancel failed", failure.message)
        assertSame(failure, assertThrows(IllegalStateException::class.java) { pump.close() })
        assertThrows(IllegalStateException::class.java) { pump.pollOnce() }
    }
}
