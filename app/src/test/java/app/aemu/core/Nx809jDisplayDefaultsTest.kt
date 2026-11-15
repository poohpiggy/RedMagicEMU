package app.aemu.core

import org.junit.Assert.assertEquals
import org.junit.Test

class Nx809jDisplayDefaultsTest {
    @Test fun newTargetInstanceDefaultsToSeventyFivePercentOfStockDisplay() {
        assertEquals(
            VmSettings(width = 912, height = 2016, density = 390),
            VmSettings().withNx809jDefaultDisplay(),
        )
    }

    @Test fun stockPresetRemainsAvailableForTheDisplaySelector() {
        assertEquals(
            VmSettings(width = 1216, height = 2688, density = 520),
            VmSettings().withNx809jDisplay(),
        )
    }
}
