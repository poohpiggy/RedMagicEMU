package app.aemu.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class GuestGameSwitchTest {
    @Test fun toggleTurnsOnFromUnsetAndOffFromOn() {
        var value: String? = null
        val commands = mutableListOf<String>()
        val sw = GuestGameSwitch { cmd ->
            commands += cmd
            when {
                cmd == "settings --user current get global virtual_game_key" -> value ?: "null"
                cmd.startsWith("settings --user current put global virtual_game_key ") -> {
                    value = cmd.substringAfterLast(' ')
                    ""
                }
                else -> error("unexpected command: $cmd")
            }
        }

        assertFalse(sw.isOn())
        assertTrue(sw.toggle())
        assertEquals("1", value)
        assertTrue(sw.isOn())
        assertFalse(sw.toggle())
        assertEquals("0", value)
        assertFalse(sw.isOn())
    }

    @Test(expected = IllegalStateException::class)
    fun setThrowsWhenReadbackDoesNotMatch() {
        val sw = GuestGameSwitch { cmd ->
            if (cmd.startsWith("settings --user current get ")) "0" else ""
        }
        sw.set(true)
    }
}
