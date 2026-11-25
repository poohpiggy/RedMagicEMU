package app.aemu.core

/**
 * Enables RedMagicOS's native virtual game-key path on the target NX809J Guest.
 * Hosts do not have the hardware side slider, so switching to virtual game-key mode lets
 * both the floating-menu Game Switch and the Control Center Game Space tile drive
 * Settings.Global.virtual_game_key through GameKeysCtrl.
 */
internal object TargetGameKeyProperty {
    fun applies(api: Int, model: String, abi: String, engine: Engine): Boolean =
        TargetRadioArtifacts.applies(api, model, abi, engine)

    fun apply(api: Int, model: String, abi: String, engine: Engine, overrides: MutableMap<String, String>) {
        if (!applies(api, model, abi, engine)) return
        overrides["ro.nubia.virtualgamekey.enable"] = "1"
        overrides["persist.sys.nubia.virtualgamekey.ctrl"] = "1"
        overrides["ro.vendor.feature.zte_feature_side_shortcut_key"] = "false"
    }
}
