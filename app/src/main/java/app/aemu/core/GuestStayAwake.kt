package app.aemu.core

/** Keeps the Guest display awake even when its emulated battery is unplugged. */
internal class GuestStayAwake(private val shell: (String) -> String) {
    @Synchronized
    fun initialize(): Boolean {
        if (read(SAVED_TIMEOUT) != null) return read(TIMEOUT) == MAX_TIMEOUT
        if (read(STAY_ON_WHILE_PLUGGED_IN) == "0") return false
        return set(true)
    }

    @Synchronized
    fun set(enabled: Boolean): Boolean {
        val timeout = read(TIMEOUT)
        val saved = read(SAVED_TIMEOUT)
        if (enabled) {
            // Save before writing the override so a retry can still restore the original timeout.
            if (saved == null || timeout != MAX_TIMEOUT) write(SAVED_TIMEOUT, timeout ?: UNSET)
            if (timeout != MAX_TIMEOUT) write(TIMEOUT, MAX_TIMEOUT)
        } else {
            if (read(STAY_ON_WHILE_PLUGGED_IN) != "0") write(STAY_ON_WHILE_PLUGGED_IN, "0")
            if (saved != null) {
                if (timeout == MAX_TIMEOUT) write(TIMEOUT, saved.takeUnless { it == UNSET })
                write(SAVED_TIMEOUT, null)
            }
        }
        return enabled
    }

    private fun read(setting: String): String? {
        val result = shell("settings --user current get $setting").trim()
        if (result == "null") return null
        check((setting == SAVED_TIMEOUT && result == UNSET) || result.toIntOrNull()?.let { it >= 0 } == true) {
            "Could not read Guest setting $setting"
        }
        return result
    }

    private fun write(setting: String, value: String?) {
        if (value == null) shell("settings --user current delete $setting")
        else shell("settings --user current put $setting $value")
        check(read(setting) == value) { "Guest setting $setting did not update" }
    }

    private companion object {
        const val TIMEOUT = "system screen_off_timeout"
        const val SAVED_TIMEOUT = "secure redmagicemu_saved_screen_off_timeout"
        const val STAY_ON_WHILE_PLUGGED_IN = "global stay_on_while_plugged_in"
        const val MAX_TIMEOUT = "2147483647"
        const val UNSET = "unset"
    }
}
