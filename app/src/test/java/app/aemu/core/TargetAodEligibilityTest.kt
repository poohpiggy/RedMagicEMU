package app.aemu.core

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class TargetAodEligibilityTest {
    private val rgb = "67ccd417f209d9d2904013b0158456997dd0db8f52f7e6806ed50540f421f636"
    private val alphaPng = "1aafc69b321aa880471e7f16bedba07f644608f678c512d5b732564f7a314849"
    private fun verified() = TargetAodEligibility.Evidence(
        model = "NX809J", api = 36, abi = "arm64-v8a", engine = Engine.N64,
        firmwareFingerprint = InstanceAdmission.SYSTEM_FINGERPRINT,
        width = 1216, height = 2688, density = 520,
        modelSha256 = "d2f72574ac8f68144e2be04f168ad1eafe45ef043a279388f0b2fd36d6cb695f",
        themeJpegSha256 = "ce26c1077f1ea969c651c286ade99251025a14aefbf7f067703c18402223a3a7",
        sourceRgbSha256 = rgb, guestRgbSha256 = rgb, guestRgbVerified = true,
        alphaPngSha256 = alphaPng, oemAodAndDepthEnabled = true, freshAndUntouched = true,
    )

    @Test fun acceptsOnlyCompleteMatchingEvidence() {
        assertTrue(TargetAodEligibility.eligible(verified()))
    }

    @Test fun refusesUnknownOrWrongDeviceAndDisplay() {
        val e = verified()
        listOf(e.copy(model = null), e.copy(model = "NX809J2"), e.copy(api = 35),
            e.copy(abi = null), e.copy(abi = "armeabi-v7a"), e.copy(engine = Engine.KK),
            e.copy(firmwareFingerprint = null), e.copy(firmwareFingerprint = "unverified"),
            e.copy(width = 1215), e.copy(height = null), e.copy(density = 521))
            .forEach { assertFalse(TargetAodEligibility.eligible(it)) }
    }

    @Test fun refusesUnknownOrMismatchedInputAndProvenance() {
        val e = verified()
        listOf(e.copy(modelSha256 = null), e.copy(modelSha256 = "0".repeat(64)),
            e.copy(themeJpegSha256 = "0".repeat(64)), e.copy(sourceRgbSha256 = "0".repeat(64)),
            e.copy(guestRgbSha256 = null), e.copy(guestRgbSha256 = "0".repeat(64)),
            e.copy(guestRgbVerified = false), e.copy(guestRgbVerified = null))
            .forEach { assertFalse(TargetAodEligibility.eligible(it)) }
    }

    @Test fun refusesMissingChangedOrMalformedAlphaResource() {
        val e = verified()
        listOf(null, "0".repeat(64), "", alphaPng + " ", "g".repeat(64)).forEach {
            assertFalse(TargetAodEligibility.eligible(e.copy(alphaPngSha256 = it)))
        }
        assertTrue(TargetAodEligibility.eligible(e.copy(alphaPngSha256 = alphaPng.uppercase())))
    }

    @Test fun refusesDisabledOrUnknownOemAodAndDepthAndUncertainInstanceHistory() {
        val e = verified()
        listOf(e.copy(oemAodAndDepthEnabled = null), e.copy(oemAodAndDepthEnabled = false),
            e.copy(freshAndUntouched = null), e.copy(freshAndUntouched = false))
            .forEach { assertFalse(TargetAodEligibility.eligible(it)) }
    }

    @Test fun acceptsUppercaseHexButRejectsMalformedDigests() {
        val e = verified()
        assertTrue(TargetAodEligibility.eligible(e.copy(guestRgbSha256 = rgb.uppercase())))
        listOf("", "x".repeat(64), rgb + " ", rgb.dropLast(1)).forEach {
            assertFalse(TargetAodEligibility.eligible(e.copy(guestRgbSha256 = it)))
        }
    }
}
