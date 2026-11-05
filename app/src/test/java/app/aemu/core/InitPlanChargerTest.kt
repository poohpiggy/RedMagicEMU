package app.aemu.core

import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File

class InitPlanChargerTest {
    @get:Rule val temp = TemporaryFolder()

    @Test fun normalBootOmitsZteOffModeChargerButKeepsHealth() {
        val root = temp.root
        for (path in listOf("system/bin/ztecharger", "vendor/bin/hw/android.hardware.health-service.qti")) {
            temp.newFolder(*path.substringBeforeLast('/').split('/').toTypedArray())
            temp.newFile(path)
        }
        val rc = File(root, "init.rc").apply {
            writeText("""
                on charger
                    class_start ztecharger

                service ztecharger /system/bin/ztecharger
                    class ztecharger

                service vendor.health-default /vendor/bin/hw/android.hardware.health-service.qti
                    class hal
            """.trimIndent())
        }

        assertEquals(listOf("vendor.health-default"), InitPlan.plan(InitPlan.parse(listOf(rc)), 36, root).map { it.name })
    }
}
