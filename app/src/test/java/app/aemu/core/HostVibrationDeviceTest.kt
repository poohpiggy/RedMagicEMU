package app.aemu.core

import org.junit.Assert.assertNull
import org.junit.Test

class HostVibrationDeviceTest {
    @Test fun missingVibratorCannotCreateAWorkingControl() {
        assertNull(HostVibrationDevice.fromVibrator(null))
    }
}
