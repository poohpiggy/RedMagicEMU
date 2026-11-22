package app.aemu.core

/** Bounds recovery while NX809J's BatteryService is waiting for its stock Health HAL. */
internal class TargetHealthStartup {
    enum class Action { WAIT, RESTART, FINISH }

    private var wasMissing = false
    private var finished = false

    fun observe(battery: Boolean?, health: Boolean?): Action {
        if (finished || battery == true) {
            finished = true
            return Action.FINISH
        }
        val missing = battery == false && health == false
        if (wasMissing && missing) {
            finished = true
            return Action.RESTART
        }
        wasMissing = missing
        return Action.WAIT
    }

    companion object {
        const val HEALTH_INTERFACE = "android.hardware.health.IHealth/default"
        private const val HEALTH_SERVICE = "vendor.health-default"
        private const val HEALTH_BINARY = "/vendor/bin/hw/android.hardware.health-service.qti"

        fun select(api: Int, model: String, abi: String, engine: Engine, recovery: Boolean,
                   plan: List<GuestService>): GuestService? {
            if (engine != Engine.N64 || api != 36 || model != "NX809J" || abi != "arm64-v8a" || recovery) return null
            return plan.singleOrNull { it.name == HEALTH_SERVICE }
                ?.takeIf { !it.onDemand && it.argv == listOf(HEALTH_BINARY) }
        }

        fun servicePresent(name: String, result: BoundedGuestCommand.Result): Boolean? {
            if (result !is BoundedGuestCommand.Result.Completed || result.exitCode != 0) return null
            return when (result.output.trim()) {
                "Service $name: found" -> true
                "Service $name: not found" -> false
                else -> null
            }
        }
    }
}
