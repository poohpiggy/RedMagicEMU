package app.aemu.core

/** Toggles RedMagicOS's virtual hardware game switch (Settings.Global.virtual_game_key). */
internal class GuestGameSwitch(private val shell: (String) -> String) {
    @Synchronized
    fun isOn(): Boolean = read() == ON

    @Synchronized
    fun toggle(): Boolean = set(!isOn())

    @Synchronized
    fun set(enabled: Boolean): Boolean {
        val target = if (enabled) ON else OFF
        shell("settings --user current put $SETTING $target")
        check(read() == target) { "Guest setting $SETTING did not update to $target" }
        return enabled
    }

    private fun read(): String? {
        val result = shell("settings --user current get $SETTING").trim()
        if (result == "null" || result.isEmpty()) return null
        check(result == ON || result == OFF) { "Unexpected Guest setting $SETTING value: $result" }
        return result
    }

    private companion object {
        const val SETTING = "global virtual_game_key"
        const val OFF = "0"
        const val ON = "1"
    }
}
