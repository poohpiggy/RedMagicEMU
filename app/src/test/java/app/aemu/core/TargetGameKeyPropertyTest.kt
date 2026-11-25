package app.aemu.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class TargetGameKeyPropertyTest {
    private val expected = mapOf(
        "ro.nubia.virtualgamekey.enable" to "1",
        "persist.sys.nubia.virtualgamekey.ctrl" to "1",
        "ro.vendor.feature.zte_feature_side_shortcut_key" to "false",
    )

    @Test fun targetEnablesVirtualGameKeyWhileKeepingOtherProperties() {
        val props = mutableMapOf(
            "ro.vendor.feature.zte_feature_side_shortcut_key" to "true",
            "unrelated" to "retained",
        )
        assertTrue(TargetGameKeyProperty.applies(36, "NX809J", "arm64-v8a", Engine.N64))
        TargetGameKeyProperty.apply(36, "NX809J", "arm64-v8a", Engine.N64, props)
        assertEquals(expected + ("unrelated" to "retained"), props)
    }

    @Test fun otherTargetsAreUnchanged() {
        val cases = listOf(
            Triple(35, "NX809J", "arm64-v8a"),
            Triple(36, "OTHER", "arm64-v8a"),
            Triple(36, "NX809J", "armeabi-v7a"),
        )
        for ((api, model, abi) in cases) {
            val props = mutableMapOf("ro.vendor.feature.zte_feature_side_shortcut_key" to "true")
            assertFalse(TargetGameKeyProperty.applies(api, model, abi, Engine.N64))
            TargetGameKeyProperty.apply(api, model, abi, Engine.N64, props)
            assertEquals(mapOf("ro.vendor.feature.zte_feature_side_shortcut_key" to "true"), props)
        }
        val otherEngine = mutableMapOf("ro.vendor.feature.zte_feature_side_shortcut_key" to "true")
        assertFalse(TargetGameKeyProperty.applies(36, "NX809J", "arm64-v8a", Engine.KK))
        TargetGameKeyProperty.apply(36, "NX809J", "arm64-v8a", Engine.KK, otherEngine)
        assertEquals(mapOf("ro.vendor.feature.zte_feature_side_shortcut_key" to "true"), otherEngine)
    }
}
