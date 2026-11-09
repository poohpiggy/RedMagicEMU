package app.aemu.core

/** The target firmware must not advertise the unavailable vendor RichTap bridge. */
internal object TargetRichTapProperty {
    fun apply(api: Int, model: String, abi: String, engine: Engine, overrides: MutableMap<String, String>) {
        if (TargetRadioArtifacts.applies(api, model, abi, engine)) {
            overrides["ro.vendor.feature.zte_feature_richtap_mode"] = "false"
        }
    }
}
