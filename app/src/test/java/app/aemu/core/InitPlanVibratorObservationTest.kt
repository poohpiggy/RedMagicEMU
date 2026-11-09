package app.aemu.core

import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

/** Offline observation of the current planner, using the target ROM's service fields. */
class InitPlanVibratorObservationTest {
    @get:Rule val temp = TemporaryFolder()

    @Test fun stockRcSelectsVibratorWhenExecutableExists() {
        val root = temp.root
        val executable = File(root, "vendor/bin/hw/vendor.qti.hardware.vibrator.service")
        executable.parentFile!!.mkdirs()
        val rc = File(root, "vibrator.rc").apply {
            writeText("""
                service vendor.qti.vibrator /vendor/bin/hw/vendor.qti.hardware.vibrator.service
                    class hal
                    user system
                    group system input
            """.trimIndent())
        }
        val parsed = InitPlan.parse(listOf(rc))
        val service = parsed.services.getValue("vendor.qti.vibrator")
        assertEquals("hal", service.cls)
        assertFalse(service.disabled)
        assertTrue(InitPlan.plan(parsed, 36, root).none { it.name == service.name })
        assertTrue(executable.createNewFile())
        val selected = InitPlan.plan(parsed, 36, root).single { it.name == service.name }
        assertEquals(service.argv, selected.argv)
        assertFalse(selected.onDemand)
        println("VIBRATOR_STOCK_RC_INCLUDED_IN_PLAN")
    }
}
