package app.aemu.core

import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class InitPlanBluetoothTest {
    @get:Rule val temp = TemporaryFolder()

    @Test fun isolatedQtiBluetoothHalIsNotStarted() {
        val root = temp.root
        val hal = File(root, "vendor/bin/hw/android.hardware.bluetooth@aidl-service-qti")
        hal.parentFile!!.mkdirs()
        hal.createNewFile()
        val health = File(root, "vendor/bin/hw/android.hardware.health-service.qti")
        health.createNewFile()
        val rc = File(root, "services.rc").apply {
            writeText("""
                service vendor.bluetooth-aidl-qti /vendor/bin/hw/android.hardware.bluetooth@aidl-service-qti
                    class hal
                service vendor.health-default /vendor/bin/hw/android.hardware.health-service.qti
                    class hal
            """.trimIndent())
        }
        assertEquals(listOf("vendor.health-default"), InitPlan.plan(InitPlan.parse(listOf(rc)), 36, root).map { it.name })
    }
}
