package app.aemu.core

import org.junit.Assert.*
import org.junit.Test

class TargetWifiLaunchTest {
    @Test fun failedOrDisabledPreparationCannotSelectWifiWhenStaleFilesExist() {
        var attempted = false
        val disabled = TargetWifiLaunch.prepare(false, { attempted = true }, { fail("disabled preparation failed") })
        assertFalse(attempted)
        assertFalse(disabled)
        var reported = false
        val failed = TargetWifiLaunch.prepare(true, { throw java.io.IOException("missing asset") }, { reported = true })
        assertTrue(reported)
        assertFalse(failed)
        assertFalse(TargetWifiLaunch.select(failed, 36, "NX809J", "arm64-v8a", Engine.N64, true, true, true))
        assertTrue(TargetWifiLaunch.prepare(true, {}, { fail("successful preparation failed") }))
    }
    @Test fun onlyReadyTargetWithAllArtifactsCanUseWifi() {
        assertFalse(TargetWifiLaunch.select(false, 36, "NX809J", "arm64-v8a", Engine.N64, true, true, true))
        assertTrue(TargetWifiLaunch.select(true, 36, "NX809J", "arm64-v8a", Engine.N64, true, true, true))
        assertFalse(TargetWifiLaunch.select(true, 35, "NX809J", "arm64-v8a", Engine.N64, true, true, true))
        assertFalse(TargetWifiLaunch.select(true, 36, "other", "arm64-v8a", Engine.N64, true, true, true))
        assertFalse(TargetWifiLaunch.select(true, 36, "NX809J", "armeabi-v7a", Engine.N64, true, true, true))
        assertFalse(TargetWifiLaunch.select(true, 36, "NX809J", "arm64-v8a", Engine.KK, true, true, true))
        assertFalse(TargetWifiLaunch.select(true, 36, "NX809J", "arm64-v8a", Engine.N64, false, true, true))
        assertFalse(TargetWifiLaunch.select(true, 36, "NX809J", "arm64-v8a", Engine.N64, true, false, true))
        assertFalse(TargetWifiLaunch.select(true, 36, "NX809J", "arm64-v8a", Engine.N64, true, true, false))
    }

    @Test fun inactiveNetworkCommandRemainsUnchanged() {
        assertEquals(listOf("/system/bin/app_process", "-Djava.class.path=/system/framework/aemu-stubs.jar",
            "/system/bin", "app.aemu.stub.NetStub"), TargetWifiLaunch.networkService(false, false).argv)
        assertEquals(listOf("/system/bin/app_process", "-Djava.class.path=/system/framework/aemu-stubs.jar",
            "/system/bin", "app.aemu.stub.NetStub", "ethernet"), TargetWifiLaunch.networkService(false, true).argv)
    }

    @Test fun enabledPathUsesIsolatedJarsAndExplicitControl() {
        assertEquals(listOf("/system/bin/app_process", "-Djava.class.path=/system/framework/redmagic-wifi-net.jar",
            "/system/bin", "app.aemu.stub.NetStub", "ethernet", "redmagic-control"),
            TargetWifiLaunch.networkService(true, true).argv)
        assertEquals(listOf("/system/bin/app_process", "-Djava.class.path=/system/framework/redmagic-wifi.jar",
            "/system/bin", "app.aemu.stub.WifiStub"), TargetWifiLaunch.wifiService().argv)
        assertTrue(TargetWifiLaunch.networkService(true, true).optional)
        assertTrue(TargetWifiLaunch.wifiService().optional)
    }

    @Test fun dnsVerifierUsesAuthenticatedGuestEntryAndValidatesArguments() {
        val service = TargetWifiLaunch.dnsVerifier("rme_test", 123, 456, 7)
        assertEquals("redmagic-wifi-dns", service.name)
        assertEquals(listOf("/system/bin/app_process", "-Djava.class.path=/system/framework/redmagic-wifi.jar",
            "/system/bin", "app.aemu.stub.WifiDnsVerifier", "--dns-verifier", "rme_test", "123", "456", "7"), service.argv)
        assertTrue(service.optional)
        for (invalid in listOf("", "bad/name", "x".repeat(81))) {
            try { TargetWifiLaunch.dnsVerifier(invalid, 123, 456, 7); fail("invalid socket accepted") }
            catch (_: IllegalArgumentException) { }
        }
        try { TargetWifiLaunch.dnsVerifier("valid", 0, 456, 7); fail("invalid PID accepted") }
        catch (_: IllegalArgumentException) { }
    }
}
