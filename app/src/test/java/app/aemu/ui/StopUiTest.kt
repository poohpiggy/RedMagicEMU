package app.aemu.ui

import org.junit.Assert.*
import org.junit.Test

class StopUiTest {
    @Test fun failedStopDoesNotCloseOrClaimSuccessAndAllowsRetry() {
        var closed = false
        var error: String? = null
        stopForUi({ throw IllegalStateException("cleanup failed") }, { closed = true }, { error = it.message })
        assertFalse(closed)
        assertEquals("cleanup failed", error)
        stopForUi({}, { closed = true }, { error = it.message })
        assertTrue(closed)
    }
    @Test fun powerOffRetryWaitsForPendingStop() {
        assertFalse(allowPowerOffRetry(true))
        assertTrue(allowPowerOffRetry(false))
    }}
