package app.aemu.ui

import org.junit.Assert.assertEquals
import org.junit.Test

class InstanceRamChoicesTest {
    @Test fun sliderOffersOnlyTheRequestedPresets() {
        assertEquals(listOf(4096, 6144, 8192, 12288, 16384), INSTANCE_RAM_CHOICES_MB)
        INSTANCE_RAM_CHOICES_MB.forEach { assertEquals(it, instanceRamChoiceMb(it)) }
    }

    @Test fun olderSettingsStartAtTheClosestPreset() {
        assertEquals(4096, instanceRamChoiceMb(0))
        assertEquals(4096, instanceRamChoiceMb(3072))
        assertEquals(8192, instanceRamChoiceMb(10240))
        assertEquals(12288, instanceRamChoiceMb(11000))
        assertEquals(16384, instanceRamChoiceMb(24576))
    }
}
