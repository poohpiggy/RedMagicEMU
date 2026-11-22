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

    @Test fun activeGuestRequestNeedsARunningVisibleEnabledAndPermittedBridge() {
        assertTrue(hostLocationCanListen(true, true, true, true, true))
        assertFalse(hostLocationCanListen(true, true, false, true, true))
        assertFalse(hostLocationCanListen(true, false, true, true, true))
        assertFalse(hostLocationCanListen(false, true, true, true, true))
        assertFalse(hostLocationCanListen(true, true, true, true, false))
    }

    @Test fun idleGuestDoesNotActivateHostLocation() {
        assertFalse(hostLocationCanListen(
            running = true, visible = true, enabled = true, demanded = false, permission = true,
        ))
    }

    @Test fun onlyAnUnexpiredBoundedGuestLeaseActivatesHostLocation() {
        val now = 100_000L
        assertTrue(HostLocationDemand.isActive("1 106000\n", now))
        assertTrue(HostLocationDemand.isActive("1 100001\n", now))
        assertFalse(HostLocationDemand.isActive("1 100001\n", now + 1))
        assertFalse(HostLocationDemand.isActive("1 106001\n", now))
        assertFalse(HostLocationDemand.isActive("1 9999999999999999999\n", now))
        assertFalse(HostLocationDemand.isActive("1 100001\nextra", now))
        assertFalse(HostLocationDemand.isActive("1 100001", now))
        assertFalse(HostLocationDemand.isActive("0\n", now))
        assertFalse(HostLocationDemand.isActive("", now))
    }

    @Test fun onlyRecentHostFixesCanBeForwarded() {
        val now = 100_000_000_000L
        assertTrue(HostLocationProtocol.isFresh(now - 30_000_000_000L, now))
        assertFalse(HostLocationProtocol.isFresh(now - 30_000_000_001L, now))
        assertFalse(HostLocationProtocol.isFresh(now + 1, now))
        assertFalse(HostLocationProtocol.isFresh(0, 1))
    }
}
