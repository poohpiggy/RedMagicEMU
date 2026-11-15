package app.aemu.ui

import app.aemu.core.TargetNetworkMode
import app.aemu.core.TargetVirtualWifiConfig
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class WifiDetailsVisibilityTest {
    @Test fun detailsFollowTheSavedNetworkMode() {
        assertTrue(showWifiDetails(true, TargetNetworkMode.WIFI))
        assertFalse(showWifiDetails(true, TargetNetworkMode.ETHERNET))
        assertFalse(showWifiDetails(true, TargetNetworkMode.LTE))
        assertFalse(showWifiDetails(false, TargetNetworkMode.WIFI))
    }

    @Test fun ethernetKeepsThePreviouslyValidWifiConfigWhenDraftIsInvalid() {
        val previous = TargetVirtualWifiConfig("Previous", -55)
        val invalidDraft = TargetVirtualWifiConfig("", -45)
        assertEquals(previous, wifiConfigForSave(true, TargetNetworkMode.ETHERNET, invalidDraft, previous))
        assertNull(wifiConfigForSave(true, TargetNetworkMode.WIFI, invalidDraft, previous))
        val validDraft = TargetVirtualWifiConfig("New", -40)
        assertEquals(validDraft, wifiConfigForSave(true, TargetNetworkMode.ETHERNET, validDraft, previous))
        assertNull(wifiConfigForSave(true, TargetNetworkMode.ETHERNET, invalidDraft, invalidDraft))
    }
}
