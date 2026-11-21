package app.aemu.core

import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class TargetCameraModeRuntimeTest {
    @get:Rule val directory = TemporaryFolder()

    private val stock = """
        <permissions>
            <library name="vendor.zte.hardware.camera.transferclient"
                file="/system_ext/framework/vendor.zte.hardware.camera.transferclient.jar" />
        </permissions>
    """.trimIndent()

    @Test fun addsTheAdapterAsADependencyAndRestoresTheStockRegistration() {
        val root = directory.newFolder()
        val registration = File(root, "system_ext/etc/permissions/vendor.zte.hardware.camera.transferclient.xml")
            .apply { requireNotNull(parentFile).mkdirs(); writeText(stock) }
        val original = File(root, "system_ext/framework/vendor.zte.hardware.camera.transferclient.jar")
            .apply { requireNotNull(parentFile).mkdirs(); writeText("stock transfer client") }
        val adapter = File(original.parentFile, "rme-camera-adapter.jar")
        val runtime = TargetCameraModeRuntime(root) { "adapter".byteInputStream() }

        assertTrue(runtime.install())
        assertTrue(registration.readText().contains("""dependency="rme-nubia-camera-adapter""""))
        assertTrue(registration.readText().contains(original.name))
        assertEquals("stock transfer client", original.readText())
        assertEquals("adapter", adapter.readText())
        assertTrue(runtime.install()) // recover a launch interrupted before cleanup

        runtime.restore()
        assertEquals(stock, registration.readText())
        assertEquals("stock transfer client", original.readText())
        assertFalse(adapter.exists())
    }
}
