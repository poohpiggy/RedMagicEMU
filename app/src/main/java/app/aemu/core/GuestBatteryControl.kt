package app.aemu.core

internal data class GuestBatteryState(val level: Int, val source: Int)

/** Manual Guest battery controls used by the floating menu and instance settings. */
internal object GuestBatteryControl {
    const val NONE = 0
    const val AC = 1
    const val USB = 2
    const val WIRELESS = 4
    const val RESET_COMMAND = "dumpsys battery reset 2>/dev/null"

    fun read(dumpsys: String): GuestBatteryState? {
        fun field(name: String): String? = dumpsys.lineSequence()
            .map { it.trim() }
            .firstOrNull { it.startsWith("$name:") }
            ?.substringAfter(':')
            ?.trim()

        val level = field("level")?.toIntOrNull()?.takeIf { it in 0..100 } ?: return null
        val status = field("status") ?: return null
        val statusCode = status.toIntOrNull()
        if (statusCode != null && statusCode !in 1..5) return null
        val source = when {
            field("Wireless powered") == "true" -> WIRELESS
            field("USB powered") == "true" -> USB
            field("AC powered") == "true" -> AC
            else -> NONE
        }
        return GuestBatteryState(level, source)
    }

    fun command(state: GuestBatteryState): String {
        require(state.level in 0..100) { "invalid battery level" }
        require(state.source in setOf(NONE, AC, USB, WIRELESS)) { "invalid battery source" }
        val charging = state.source != NONE
        val status = if (charging) 2 else 3
        fun bit(source: Int) = if (state.source == source) 1 else 0
        return "echo ${state.level} > /sys/class/power_supply/battery/capacity 2>/dev/null; " +
            "echo ${bit(AC)} > /sys/class/power_supply/ac/online 2>/dev/null; " +
            "dumpsys battery set level ${state.level}; dumpsys battery set status $status; " +
            "dumpsys battery set ac ${bit(AC)}; dumpsys battery set usb ${bit(USB)}; " +
            "dumpsys battery set wireless ${bit(WIRELESS)}"
    }
}
