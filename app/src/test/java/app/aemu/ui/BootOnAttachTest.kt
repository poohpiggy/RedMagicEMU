package app.aemu.ui

import app.aemu.core.GuestVm
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class BootOnAttachTest {
    @Test fun failedVmIsNotAutomaticallyRetried() {
        assertFalse(shouldBootOnAttach(GuestVm.State.FAILED))
        assertTrue(shouldBootOnAttach(GuestVm.State.STOPPED))
    }
    @Test fun stoppedGuestAfterPowerOffIsNotRestartedOnPageRecreation() {
        assertFalse(shouldBootOnAttach(GuestVm.State.STOPPED, false, true))
        assertTrue(shouldBootOnAttach(GuestVm.State.STOPPED, false, false))
    }}
