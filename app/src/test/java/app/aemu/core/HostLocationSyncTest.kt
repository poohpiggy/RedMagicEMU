package app.aemu.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class HostLocationSyncTest {
    @Test fun roundTripsHostLocationWithoutLocaleDependentNumbers() {
        val reading = HostLocationProtocol.reading(
            latitude = 30.381479,
            longitude = 114.332406,
            altitude = 43.25,
            accuracy = 30f,
            elapsedRealtimeNanos = 124_507_123_456_789L,
            timeMillis = 1_794_760_123_456L,
        )

        assertEquals(reading, HostLocationProtocol.parse(reading!!.encode()))
    }

    @Test fun rejectsMalformedOrImpossibleCoordinates() {
        assertNull(HostLocationProtocol.reading(91.0, 0.0, null, 1f, 1L, 1L))
        assertNull(HostLocationProtocol.reading(0.0, 181.0, null, 1f, 1L, 1L))
        assertNull(HostLocationProtocol.reading(0.0, 0.0, null, -1f, 1L, 1L))
        assertNull(HostLocationProtocol.reading(0.0, 0.0, null, 1f, 0L, 1L))
        assertNull(HostLocationProtocol.parse("0\n"))
        assertNull(HostLocationProtocol.parse("1 nope 0 null 1 1 1\n"))
    }

    @Test fun locationBridgeUsesTheSameEnableGateAsMotionSensors() {
        assertTrue(hostLocationCanListen(true, true, true, true))
        assertFalse(hostLocationCanListen(true, true, false, true))
        assertFalse(hostLocationCanListen(true, false, true, true))
        assertFalse(hostLocationCanListen(false, true, true, true))
        assertFalse(hostLocationCanListen(true, true, true, false))
    }
}
