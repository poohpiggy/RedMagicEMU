package app.aemu.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class GuestBatteryControlTest {
    @Test fun dumpsysStateReportsLevelAndWirelessSource() {
        val state = GuestBatteryControl.read(
            """
            Current Battery Service state:
              AC powered: false
              USB powered: false
              Wireless powered: true
              status: 2
              level: 73
            """.trimIndent(),
        )

        assertEquals(GuestBatteryState(level = 73, source = GuestBatteryControl.WIRELESS), state)
    }

    @Test fun disconnectedStateUsesGuestDischargingStatus() {
        val state = GuestBatteryControl.read(
            """
              AC powered: false
              USB powered: false
              Wireless powered: false
              status: 3
              level: 41
            """.trimIndent(),
        )

        assertEquals(GuestBatteryState(level = 41, source = GuestBatteryControl.NONE), state)
        assertEquals(
            "echo 41 > /sys/class/power_supply/battery/capacity 2>/dev/null; " +
                "echo 0 > /sys/class/power_supply/ac/online 2>/dev/null; " +
                "dumpsys battery set level 41; dumpsys battery set status 3; " +
                "dumpsys battery set ac 0; dumpsys battery set usb 0; dumpsys battery set wireless 0",
            GuestBatteryControl.command(state!!),
        )
    }

    @Test fun usbAndWirelessSelectionsClearEveryOtherSource() {
        assertEquals(
            "echo 65 > /sys/class/power_supply/battery/capacity 2>/dev/null; " +
                "echo 0 > /sys/class/power_supply/ac/online 2>/dev/null; " +
                "dumpsys battery set level 65; dumpsys battery set status 2; " +
                "dumpsys battery set ac 0; dumpsys battery set usb 1; dumpsys battery set wireless 0",
            GuestBatteryControl.command(GuestBatteryState(65, GuestBatteryControl.USB)),
        )
        assertEquals(
            "echo 66 > /sys/class/power_supply/battery/capacity 2>/dev/null; " +
                "echo 0 > /sys/class/power_supply/ac/online 2>/dev/null; " +
                "dumpsys battery set level 66; dumpsys battery set status 2; " +
                "dumpsys battery set ac 0; dumpsys battery set usb 0; dumpsys battery set wireless 1",
            GuestBatteryControl.command(GuestBatteryState(66, GuestBatteryControl.WIRELESS)),
        )
    }

    @Test fun malformedDumpsysStateIsRejected() {
        assertNull(GuestBatteryControl.read("level: 101"))
        assertNull(GuestBatteryControl.read("level: 50"))
        assertEquals(GuestBatteryState(50, GuestBatteryControl.NONE), GuestBatteryControl.read("level: 50\nstatus: 3"))
    }
}
