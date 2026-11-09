package app.aemu.core

import org.junit.Assert.assertEquals
import org.junit.Test

class TargetRichTapPropertyTest {
    private val key = "ro.vendor.feature.zte_feature_richtap_mode"

    @Test fun targetDisablesUnavailableRichTapWhileKeepingOtherProperties() {
        val props = mutableMapOf(key to "true", "unrelated" to "retained")
        TargetRichTapProperty.apply(36, "NX809J", "arm64-v8a", Engine.N64, props)
        assertEquals(mapOf(key to "false", "unrelated" to "retained"), props)
    }

    @Test fun otherTargetsAreUnchanged() {
        val cases = listOf(
            Triple(35, "NX809J", "arm64-v8a"),
            Triple(36, "OTHER", "arm64-v8a"),
            Triple(36, "NX809J", "armeabi-v7a")
        )
        for ((api, model, abi) in cases) {
            val props = mutableMapOf(key to "true", "unrelated" to "retained")
            TargetRichTapProperty.apply(api, model, abi, Engine.N64, props)
            assertEquals(mapOf(key to "true", "unrelated" to "retained"), props)
        }
        val otherEngine = mutableMapOf(key to "true", "unrelated" to "retained")
        TargetRichTapProperty.apply(36, "NX809J", "arm64-v8a", Engine.KK, otherEngine)
        assertEquals(mapOf(key to "true", "unrelated" to "retained"), otherEngine)
    }

    @Test fun targetAddsMissingProperty() {
        val props = mutableMapOf("unrelated" to "retained")
        TargetRichTapProperty.apply(36, "NX809J", "arm64-v8a", Engine.N64, props)
        assertEquals(mapOf(key to "false", "unrelated" to "retained"), props)
    }
}
