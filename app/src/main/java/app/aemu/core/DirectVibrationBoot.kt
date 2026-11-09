package app.aemu.core

/** Pure gating and rollback ordering for the target's optional direct Binder vibration bridge. */
internal object DirectVibrationBoot {
    private const val QTI_SERVICE = "/vendor/bin/hw/vendor.qti.hardware.vibrator.service"

    private fun isTarget(api: Int, model: String, abi: String, engine: Engine) =
        api == 36 && model == "NX809J" && abi == "arm64-v8a" && engine == Engine.N64

    fun eligible(enabled: Boolean, recovery: Boolean, api: Int, model: String, abi: String,
                 engine: Engine, managedBinder: Boolean, vibratorAvailable: Boolean,
                 nativeAvailable: Boolean): Boolean =
        enabled && !recovery && isTarget(api, model, abi, engine) && managedBinder &&
            vibratorAvailable && nativeAvailable

    /** Rollback is only safe before any other Guest service can cache the declaration. */
    fun validServiceOrder(plan: List<GuestService>): Boolean {
        val managers = plan.filter { it.name == "servicemanager" }
        if (managers.size != 1) return false
        val manager = managers.single()
        return !manager.onDemand && manager.argv.firstOrNull() == "/system/bin/servicemanager" &&
            plan.firstOrNull { !it.onDemand } === manager
    }

    fun skipQti(active: Boolean, api: Int, model: String, abi: String, engine: Engine,
                argv: List<String>): Boolean =
        active && isTarget(api, model, abi, engine) && argv.firstOrNull() == QTI_SERVICE

    /** Each stop must confirm exit or throw; never remove a cached declaration before both exits. */
    fun rollback(stopBridge: () -> Unit, stopManager: () -> Unit, stopBinder: () -> Unit,
                 removeDeclaration: () -> Unit, restartBinder: () -> Unit, restartManager: () -> Unit) {
        stopBridge()
        stopManager()
        stopBinder()
        removeDeclaration()
        restartBinder()
        restartManager()
    }
}