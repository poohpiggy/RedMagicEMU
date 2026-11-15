package app.aemu.core

internal object TargetAodEligibility {
    // These identify only the investigated firmware, theme, input and precomputed mask.
    private const val MODEL_SHA = "d2f72574ac8f68144e2be04f168ad1eafe45ef043a279388f0b2fd36d6cb695f"
    private const val THEME_SHA = "ce26c1077f1ea969c651c286ade99251025a14aefbf7f067703c18402223a3a7"
    private const val RGB_SHA = "67ccd417f209d9d2904013b0158456997dd0db8f52f7e6806ed50540f421f636"
    private const val ALPHA_PNG_SHA = "1aafc69b321aa880471e7f16bedba07f644608f678c512d5b732564f7a314849"

    /** Facts supplied by the caller; this predicate does not verify or collect them. */
    data class Evidence(
        val model: String? = null,
        val api: Int? = null,
        val abi: String? = null,
        val engine: Engine? = null,
        val firmwareFingerprint: String? = null,
        val width: Int? = null,
        val height: Int? = null,
        val density: Int? = null,
        val modelSha256: String? = null,
        val themeJpegSha256: String? = null,
        val sourceRgbSha256: String? = null,
        val guestRgbSha256: String? = null,
        val guestRgbVerified: Boolean? = null,
        val alphaPngSha256: String? = null,
        /** Observed OEM AOD and depth enablement; null means unknown, not a consent request. */
        val oemAodAndDepthEnabled: Boolean? = null,
        val freshAndUntouched: Boolean? = null,
    )

    fun eligible(e: Evidence): Boolean {
        if (e.model != "NX809J" || e.api != 36 || e.abi != "arm64-v8a" || e.engine != Engine.N64 ||
            e.firmwareFingerprint !in setOf(InstanceAdmission.SYSTEM_FINGERPRINT, InstanceAdmission.BOOT_FINGERPRINT) ||
            e.width != 1216 || e.height != 2688 || e.density != 520 ||
            e.oemAodAndDepthEnabled != true || e.freshAndUntouched != true || e.guestRgbVerified != true) return false
        if (!matches(e.modelSha256, MODEL_SHA) || !matches(e.themeJpegSha256, THEME_SHA) ||
            !matches(e.sourceRgbSha256, RGB_SHA) || !matches(e.guestRgbSha256, RGB_SHA)) return false
        return matches(e.alphaPngSha256, ALPHA_PNG_SHA)
    }

    private fun matches(actual: String?, expected: String): Boolean =
        actual != null && actual.length == 64 && actual.all { it in '0'..'9' || it in 'a'..'f' || it in 'A'..'F' } &&
            actual.equals(expected, ignoreCase = true)
}
