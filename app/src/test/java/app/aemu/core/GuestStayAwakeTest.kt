package app.aemu.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

class GuestStayAwakeTest {
    @Test
    fun migratesTheOldChargingOnlySwitchAndRestoresTheGuestTimeout() {
        val guest = GuestSettings(timeout = "60000", pluggedIn = "15")
        val control = GuestStayAwake(guest::shell)

        assertTrue(control.initialize())
        assertEquals("2147483647", guest.timeout)
        assertEquals("15", guest.pluggedIn)
        assertTrue(control.set(true))
        assertFalse(GuestStayAwake(guest::shell).set(false))
        assertEquals("60000", guest.timeout)
        assertEquals("0", guest.pluggedIn)
        assertFalse(GuestStayAwake(guest::shell).initialize())
    }

    @Test
    fun restoresAnUnsetTimeout() {
        val guest = GuestSettings(timeout = null, pluggedIn = "0")
        val control = GuestStayAwake(guest::shell)

        assertTrue(control.set(true))
        assertFalse(control.set(false))
        assertNull(guest.timeout)
    }

    @Test
    fun turningItOffPreservesATimeoutChangedInGuestSettings() {
        val guest = GuestSettings(timeout = "30000", pluggedIn = "15")
        val control = GuestStayAwake(guest::shell)
        assertTrue(control.set(true))
        guest.timeout = "120000"

        assertFalse(control.set(false))
        assertEquals("120000", guest.timeout)
    }

    @Test
    fun aRejectedTimeoutWriteCanBeRetriedWithoutLosingTheBackup() {
        val guest = GuestSettings(timeout = "30000", pluggedIn = "0")
        val control = GuestStayAwake(guest::shell)
        guest.rejectTimeoutWrite = true

        assertThrows(IllegalStateException::class.java) { control.set(true) }
        assertEquals("30000", guest.timeout)
        guest.rejectTimeoutWrite = false
        assertTrue(control.set(true))
        assertFalse(control.set(false))
        assertEquals("30000", guest.timeout)
    }

    private class GuestSettings(timeout: String?, pluggedIn: String?) {
        private val values = mutableMapOf(
            "system screen_off_timeout" to timeout,
            "global stay_on_while_plugged_in" to pluggedIn,
        )
        var timeout: String?
            get() = values["system screen_off_timeout"]
            set(value) { values["system screen_off_timeout"] = value }
        val pluggedIn get() = values["global stay_on_while_plugged_in"]
        var rejectTimeoutWrite = false

        fun shell(command: String): String {
            val parts = command.split(" ")
            assertEquals(listOf("settings", "--user", "current"), parts.take(3))
            val key = parts.subList(4, 6).joinToString(" ")
            return when (parts[3]) {
                "get" -> values[key] ?: "null"
                "put" -> {
                    if (key != "system screen_off_timeout" || !rejectTimeoutWrite) values[key] = parts[6]
                    ""
                }
                "delete" -> {
                    values.remove(key)
                    ""
                }
                else -> error("Unexpected settings command: $command")
            }
        }
    }
}
