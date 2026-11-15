package app.aemu.core

import org.junit.Assert.assertEquals
import org.junit.Test

class TargetRefreshComposerTest {
    @Test fun targetAloneUsesTheMultiRateComposer() {
        val original = "engines/n64-36/vendor#lib64#hw#hwcomposer.default.so"
        val candidate = "engines/redmagic/hwcomposer-refresh.so"
        assertEquals(candidate, TargetRefreshComposer.select(original, 36, "NX809J", "arm64-v8a", Engine.N64))
        assertEquals(original, TargetRefreshComposer.select(original, 35, "NX809J", "arm64-v8a", Engine.N64))
        assertEquals(original, TargetRefreshComposer.select(original, 36, "other", "arm64-v8a", Engine.N64))
        assertEquals(original, TargetRefreshComposer.select(original, 36, "NX809J", "armeabi-v7a", Engine.N64))
        assertEquals(original, TargetRefreshComposer.select(original, 36, "NX809J", "arm64-v8a", Engine.KK))
    }
}
