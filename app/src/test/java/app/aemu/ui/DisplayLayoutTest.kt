package app.aemu.ui

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class DisplayLayoutTest {
    @Test fun scaledPresetsKeepTheDisplayProportionsAndScaleDensity() {
        assertEquals(listOf(
            DisplayPreset(1440, 3200, 600),
            DisplayPreset(1080, 2400, 450),
            DisplayPreset(720, 1600, 300),
        ), scaledDisplayPresets(DisplayPreset(1440, 3200, 600)))
        assertEquals(listOf(
            DisplayPreset(1216, 2688, 520),
            DisplayPreset(912, 2016, 390),
            DisplayPreset(608, 1344, 260),
        ), scaledDisplayPresets(DisplayPreset(1216, 2688, 520)))
        assertEquals(DisplayPreset(810, 1800, 315), scaledDisplayPresets(DisplayPreset(1081, 2401, 420))[1])
    }

    @Test fun hostDisplayUsesPortraitDimensionsAndCurrentDensity() {
        assertEquals(DisplayPreset(1440, 3200, 600), hostDisplayPreset(1440, 3200, 600))
        assertEquals(DisplayPreset(1440, 3200, 600), hostDisplayPreset(3200, 1440, 600))
        assertNull(hostDisplayPreset(0, 3200, 600))
        assertNull(hostDisplayPreset(1440, 3200, 0))
    }

    @Test fun matchingScreenFillsViewportAndOtherRatiosStayUncropped() {
        assertEquals(GuestViewport(0, 0, 1440, 3200), fitGuestViewport(1440, 3200, 1440, 3200))
        assertEquals(GuestViewport(400, 0, 600, 1200), fitGuestViewport(1400, 1200, 500, 1000))
        assertEquals(GuestViewport(0, 400, 1200, 600), fitGuestViewport(1200, 1400, 1000, 500))
        assertNull(fitGuestViewport(0, 3200, 1440, 3200))
    }
}
