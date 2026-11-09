package app.aemu.core

import org.junit.Assert.*
import org.junit.Test

class DirectVibrationBootTest {
    private fun eligible(enabled: Boolean = true, recovery: Boolean = false, api: Int = 36,
                         model: String = "NX809J", abi: String = "arm64-v8a", engine: Engine = Engine.N64,
                         managedBinder: Boolean = true, vibratorAvailable: Boolean = true,
                         nativeAvailable: Boolean = true) = DirectVibrationBoot.eligible(
        enabled, recovery, api, model, abi, engine, managedBinder, vibratorAvailable, nativeAvailable)

    @Test fun bridgeRequiresExactTargetAndEveryPrerequisite() {
        assertTrue(eligible())
        assertFalse(eligible(enabled = false))
        assertFalse(eligible(recovery = true))
        assertFalse(eligible(api = 35))
        assertFalse(eligible(model = "other"))
        assertFalse(eligible(abi = "armeabi-v7a"))
        assertFalse(eligible(engine = Engine.KK))
        assertFalse(eligible(managedBinder = false))
        assertFalse(eligible(vibratorAvailable = false))
        assertFalse(eligible(nativeAvailable = false))
    }

    @Test fun planRequiresOneActiveManagerBeforeEveryOtherActiveService() {
        val manager = GuestService("servicemanager", listOf("/system/bin/servicemanager"))
        val other = GuestService("vendor", listOf("/vendor/bin/service"))
        assertTrue(DirectVibrationBoot.validServiceOrder(listOf(manager, other)))
        assertTrue(DirectVibrationBoot.validServiceOrder(listOf(other.copy(onDemand = true), manager, other)))
        assertFalse(DirectVibrationBoot.validServiceOrder(emptyList()))
        assertFalse(DirectVibrationBoot.validServiceOrder(listOf(other, manager)))
        assertFalse(DirectVibrationBoot.validServiceOrder(listOf(manager.copy(onDemand = true), other)))
        assertFalse(DirectVibrationBoot.validServiceOrder(listOf(manager, manager)))
        assertFalse(DirectVibrationBoot.validServiceOrder(listOf(manager, manager.copy(onDemand = true))))
        assertFalse(DirectVibrationBoot.validServiceOrder(listOf(manager.copy(argv = listOf("/vendor/bin/servicemanager")))))
    }

    @Test fun onlyEnabledTargetQtiExecutableIsSkipped() {
        val path = "/vendor/bin/hw/vendor.qti.hardware.vibrator.service"
        assertTrue(DirectVibrationBoot.skipQti(true, 36, "NX809J", "arm64-v8a", Engine.N64, listOf(path)))
        assertFalse(DirectVibrationBoot.skipQti(false, 36, "NX809J", "arm64-v8a", Engine.N64, listOf(path)))
        assertFalse(DirectVibrationBoot.skipQti(true, 35, "NX809J", "arm64-v8a", Engine.N64, listOf(path)))
        assertFalse(DirectVibrationBoot.skipQti(true, 36, "other", "arm64-v8a", Engine.N64, listOf(path)))
        assertFalse(DirectVibrationBoot.skipQti(true, 36, "NX809J", "arm64-v8a", Engine.N64, listOf("$path.other")))
        assertFalse(DirectVibrationBoot.skipQti(true, 36, "NX809J", "arm64-v8a", Engine.N64, emptyList()))
    }

    @Test fun rollbackWaitsForExitBeforeRemovingDeclarationAndRestarting() {
        val seen = mutableListOf<String>()
        DirectVibrationBoot.rollback({ seen += "bridge" }, { seen += "manager" }, { seen += "binder" },
            { seen += "declaration" }, { seen += "restartBinder" }, { seen += "restartManager" })
        assertEquals(listOf("bridge", "manager", "binder", "declaration", "restartBinder", "restartManager"), seen)
    }

    @Test fun rollbackFailureImmediatelyStopsLaterSteps() {
        val names = listOf("bridge", "manager", "binder", "declaration", "restartBinder", "restartManager")
        for (failed in names.indices) {
            val seen = mutableListOf<String>()
            val calls: List<() -> Unit> = names.mapIndexed { i, name -> {
                seen += name
                if (i == failed) throw IllegalStateException(name)
            } }
            try {
                DirectVibrationBoot.rollback(calls[0], calls[1], calls[2], calls[3], calls[4], calls[5])
                fail("failure must propagate")
            } catch (error: IllegalStateException) { assertEquals(names[failed], error.message) }
            assertEquals(names.take(failed + 1), seen)
        }
    }
}