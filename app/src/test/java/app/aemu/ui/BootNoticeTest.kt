package app.aemu.ui

import app.aemu.core.GuestVm
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class BootNoticeTest {
    @Test fun reportedBootCompletionDoesNotHideUnverifiedDesktopStatus() {
        assertTrue(shouldShowBootNotice(GuestVm.State.BOOTING))
        assertTrue(shouldShowBootNotice(GuestVm.State.RUNNING))
        assertFalse(shouldShowBootNotice(GuestVm.State.FAILED))
    }
}
