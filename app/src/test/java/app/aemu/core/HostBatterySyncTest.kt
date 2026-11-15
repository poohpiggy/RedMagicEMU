package app.aemu.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class HostBatterySyncTest {
    @Test fun hostReadingUpdatesLevelStatusAndEveryPlugSource() {
        val reading = HostBatteryProtocol.reading(level = 67, status = 2, plugged = 2)

        assertEquals(
            "dumpsys battery set level 67; dumpsys battery set status 2; " +
                "dumpsys battery set ac 0; dumpsys battery set usb 1; dumpsys battery set wireless 0",
            reading?.command(),
        )
    }

    @Test fun wirelessChargingClearsStaleWiredPlugState() {
        val reading = HostBatteryProtocol.reading(level = 91, status = 2, plugged = 4)

        assertEquals(
            "dumpsys battery set level 91; dumpsys battery set status 2; " +
                "dumpsys battery set ac 0; dumpsys battery set usb 0; dumpsys battery set wireless 1",
            reading?.command(),
        )
    }

    @Test fun unpluggedHostAlsoClearsChargingState() {
        val reading = HostBatteryProtocol.reading(level = 35, status = 3, plugged = 0)

        assertEquals(
            "dumpsys battery set level 35; dumpsys battery set status 3; " +
                "dumpsys battery set ac 0; dumpsys battery set usb 0; dumpsys battery set wireless 0",
            reading?.command(),
        )
    }

    @Test fun unknownOrMalformedHostValuesAreIgnored() {
        assertNull(HostBatteryProtocol.reading(level = -1, status = 3, plugged = 0))
        assertNull(HostBatteryProtocol.reading(level = 101, status = 3, plugged = 0))
        assertNull(HostBatteryProtocol.reading(level = 50, status = 0, plugged = 0))
        assertNull(HostBatteryProtocol.reading(level = 50, status = 3, plugged = -1))
        assertNull(HostBatteryProtocol.reading(level = 50, status = 3, plugged = 16))
    }

    @Test fun batterySyncSettingDefaultsOnAndCanBeDisabledPerInstance() {
        assertTrue(VmSettings().batterySync)
        assertEquals(false, VmSettings(batterySync = false).batterySync)
    }
}
