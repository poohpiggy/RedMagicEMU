package app.aemu.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class TargetHealthStartupTest {
    @Test fun missingHealthDuringBootAllowsOneRestart() {
        val startup = TargetHealthStartup()
        assertEquals(TargetHealthStartup.Action.WAIT, startup.observe(battery = false, health = true))
        assertEquals(TargetHealthStartup.Action.WAIT, startup.observe(battery = false, health = false))
        assertEquals(TargetHealthStartup.Action.RESTART, startup.observe(battery = false, health = false))
        assertEquals(TargetHealthStartup.Action.FINISH, startup.observe(battery = false, health = false))
    }

    @Test fun bootProgressOrInconclusiveProbesDoNotRestartHealth() {
        val startup = TargetHealthStartup()
        assertEquals(TargetHealthStartup.Action.WAIT, startup.observe(battery = false, health = false))
        assertEquals(TargetHealthStartup.Action.WAIT, startup.observe(battery = null, health = false))
        assertEquals(TargetHealthStartup.Action.WAIT, startup.observe(battery = false, health = false))
        assertEquals(TargetHealthStartup.Action.WAIT, startup.observe(battery = false, health = null))
        assertEquals(TargetHealthStartup.Action.WAIT, startup.observe(battery = false, health = false))
        assertEquals(TargetHealthStartup.Action.FINISH, startup.observe(battery = true, health = null))
        assertEquals(TargetHealthStartup.Action.FINISH, startup.observe(battery = false, health = false))
    }

    @Test fun onlyCompletedServiceChecksCanEstablishAbsence() {
        val service = "android.hardware.health.IHealth/default"
        fun check(code: Int, output: String) = TargetHealthStartup.servicePresent(
            service, BoundedGuestCommand.Result.Completed(code, output),
        )
        assertEquals(true, check(0, "Service $service: found\n"))
        assertEquals(false, check(0, "Service $service: not found\n"))
        assertNull(check(1, "Service $service: not found\n"))
        assertNull(check(0, "Can't find service manager\n"))
        assertNull(check(0, "Service battery: not found\n"))
        assertNull(TargetHealthStartup.servicePresent(service, BoundedGuestCommand.Result.Uncertain(
            BoundedGuestCommand.Reason.TIMEOUT, false, false, false, true,
        )))
    }

    @Test fun targetsOnlyTheStockNx809jHealthService() {
        val binary = "/vendor/bin/hw/android.hardware.health-service.qti"
        val health = GuestService("vendor.health-default", listOf(binary))
        val charger = GuestService("vendor.charger", listOf(binary, "--charger"))
        val plan = listOf(charger, health)
        assertEquals(health, TargetHealthStartup.select(36, "NX809J", "arm64-v8a", Engine.N64, false, plan))
        assertNull(TargetHealthStartup.select(35, "NX809J", "arm64-v8a", Engine.N64, false, plan))
        assertNull(TargetHealthStartup.select(36, "Other", "arm64-v8a", Engine.N64, false, plan))
        assertNull(TargetHealthStartup.select(36, "NX809J", "arm64-v8a", Engine.N64, true, plan))
        assertNull(TargetHealthStartup.select(36, "NX809J", "arm64-v8a", Engine.N64, false, listOf(charger)))
        assertNull(TargetHealthStartup.select(36, "NX809J", "arm64-v8a", Engine.N64, false, listOf(health, health)))
    }
}
