package app.aemu.core

import org.junit.Assert.*
import org.junit.Test

class TargetVirtualWifiConfigTest {
    @Test fun defaultConfigIsValid() {
        val config = TargetVirtualWifiConfig()
        assertEquals("RedMagicEMU", config.ssid)
        assertTrue(config.isValid())
    }

    @Test fun acceptsSupportedApValuesAndRejectsInvalidInputs() {
        val config = TargetVirtualWifiConfig()
        assertTrue(config.copy(ssid = "热点", rssiDbm = -100).isValid())
        assertTrue(config.copy(rssiDbm = -30).isValid())
        assertFalse(config.copy(ssid = "").isValid())
        assertFalse(config.copy(ssid = "热".repeat(11)).isValid())
        assertFalse(config.copy(ssid = "bad\nname").isValid())
        assertFalse(config.copy(rssiDbm = -101).isValid())
        assertFalse(config.copy(rssiDbm = -29).isValid())
    }
}
