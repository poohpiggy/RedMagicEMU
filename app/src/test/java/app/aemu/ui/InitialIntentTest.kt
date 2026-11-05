package app.aemu.ui

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class InitialIntentTest {
    @Test fun restoredActivityDoesNotRedeliverOriginalLaunchIntent() {
        assertTrue(shouldHandleInitialIntent(restored = false))
        assertFalse(shouldHandleInitialIntent(restored = true))
    }
}
