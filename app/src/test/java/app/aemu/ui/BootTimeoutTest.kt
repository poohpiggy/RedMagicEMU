package app.aemu.ui

import app.aemu.core.GuestVm
import org.junit.Assert.*
import org.junit.Test

class BootTimeoutTest {
    @Test fun bootWaitingBecomesUnconfirmedAtThreshold() {
        assertFalse(bootUnconfirmed(GuestVm.State.BOOTING, 299_999L))
        assertTrue(bootUnconfirmed(GuestVm.State.BOOTING, 300_000L))
    }

    @Test fun onlyInProgressStatesCanTimeOut() {
        assertTrue(bootUnconfirmed(GuestVm.State.PREPARING, 300_000L))
        assertFalse(bootUnconfirmed(GuestVm.State.RUNNING, 300_000L))
        assertFalse(bootUnconfirmed(GuestVm.State.FAILED, 300_000L))
        assertFalse(bootUnconfirmed(GuestVm.State.STOPPED, 300_000L))
    }

    @Test fun dismissedWarningIsHiddenForAttempt() {
        assertTrue(bootWarningVisible(GuestVm.State.BOOTING, 300_000L, false))
        assertFalse(bootWarningVisible(GuestVm.State.BOOTING, 300_000L, true))
        assertTrue(bootWarningVisible(GuestVm.State.BOOTING, 300_000L, false)) // new attempt
        assertFalse(bootWarningVisible(GuestVm.State.RUNNING, 300_000L, false))
    }

}



