package app.aemu.core

import org.junit.Assert.*
import org.junit.Test
import android.content.SharedPreferences
import java.lang.reflect.Proxy

class TargetNetworkModeTest {
    @Test fun absentOrUnknownPreferenceUsesEthernet() {
        assertEquals(TargetNetworkMode.ETHERNET, TargetNetworkMode.parse(null))
        assertEquals(TargetNetworkMode.ETHERNET, TargetNetworkMode.parse("unexpected"))
        assertEquals(TargetNetworkMode.ETHERNET, TargetNetworkMode.parse("ethernet"))
        assertEquals(TargetNetworkMode.WIFI, TargetNetworkMode.parse("wifi"))
        assertEquals(TargetNetworkMode.LTE, TargetNetworkMode.parse("lte"))
    }

    @Test fun preferenceKeysAreInstanceScopedAndRejectInvalidIds() {
        assertNotEquals(TargetNetworkMode.key("abcdef"), TargetNetworkMode.key("ghjkmn"))
        for (id in listOf("", "../outside", "ABCDEF")) {
            try { TargetNetworkMode.key(id); fail("invalid instance ID accepted") }
            catch (_: IllegalArgumentException) { }
        }
    }

    @Test fun choiceNeverBypassesReadinessOrTargetChecks() {
        assertFalse(TargetNetworkMode.ETHERNET.prepareWifi(runtimeReady = true, target = true))
        assertFalse(TargetNetworkMode.WIFI.prepareWifi(runtimeReady = false, target = true))
        assertFalse(TargetNetworkMode.WIFI.prepareWifi(runtimeReady = true, target = false))
        assertTrue(TargetNetworkMode.WIFI.prepareWifi(runtimeReady = true, target = true))
        assertFalse(TargetNetworkMode.ETHERNET.prepareLte(runtimeReady = true, target = true, radio = true))
        assertFalse(TargetNetworkMode.LTE.prepareLte(runtimeReady = false, target = true, radio = true))
        assertFalse(TargetNetworkMode.LTE.prepareLte(runtimeReady = true, target = false, radio = true))
        assertFalse(TargetNetworkMode.LTE.prepareLte(runtimeReady = true, target = true, radio = false))
        assertTrue(TargetNetworkMode.LTE.prepareLte(runtimeReady = true, target = true, radio = true))
    }

    @Test fun preferenceRoundTripIsInstanceScopedAndCommitFailureIsReported() {
        val saved = mutableMapOf<String, Any>()
        var rejectCommit = false
        var pending = mutableMapOf<String, String>()
        val editor = Proxy.newProxyInstance(SharedPreferences.Editor::class.java.classLoader,
            arrayOf(SharedPreferences.Editor::class.java)) { proxy, method, args ->
            when (method.name) {
                "putString" -> { pending[args!![0] as String] = args[1] as String; proxy }
                "commit" -> if (rejectCommit) false else { saved.putAll(pending); true }
                else -> throw UnsupportedOperationException(method.name)
            }
        } as SharedPreferences.Editor
        val preferences = Proxy.newProxyInstance(SharedPreferences::class.java.classLoader,
            arrayOf(SharedPreferences::class.java)) { _, method, args ->
            when (method.name) {
                "getString" -> saved[args!![0] as String] ?: args[1]
                "getAll" -> saved.toMap()
                "edit" -> { pending = mutableMapOf(); editor }
                else -> throw UnsupportedOperationException(method.name)
            }
        } as SharedPreferences

        assertEquals(TargetNetworkMode.ETHERNET, TargetNetworkMode.read(preferences, "abcdef"))
        assertTrue(TargetNetworkMode.write(preferences, "abcdef", TargetNetworkMode.WIFI))
        assertEquals(TargetNetworkMode.WIFI, TargetNetworkMode.read(preferences, "abcdef"))
        assertEquals(TargetNetworkMode.ETHERNET, TargetNetworkMode.read(preferences, "ghjkmn"))
        rejectCommit = true
        assertFalse(TargetNetworkMode.write(preferences, "abcdef", TargetNetworkMode.ETHERNET))
        assertEquals(TargetNetworkMode.WIFI, TargetNetworkMode.read(preferences, "abcdef"))
        saved[TargetNetworkMode.key("abcdef")] = 42
        assertEquals(TargetNetworkMode.ETHERNET, TargetNetworkMode.read(preferences, "abcdef"))
    }
}
