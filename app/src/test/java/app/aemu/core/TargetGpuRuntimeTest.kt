package app.aemu.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File
import java.security.MessageDigest

class TargetGpuRuntimeTest {
    @get:Rule val folder = TemporaryFolder()

    @Test fun hidesOnlyVulkanFeaturesUntilTheBridgeIsEnabledAgain() {
        val root = folder.newFolder()
        val permissions = File(root, "system/etc/permissions").apply { mkdirs() }
        val overlay = File(permissions, "aemu-vulkan-unavailable.xml")
        val stock = File(root, "vendor/etc/permissions/graphics.xml").apply { parentFile!!.mkdirs() }
        val original = """<permissions><feature name="android.hardware.vulkan.version" version="4210688" /></permissions>"""
        stock.writeText(original)
        val runtime = TargetGpuRuntime(root, folder.newFolder()) { error("Feature settings do not load GPU libraries") }

        runtime.setVulkanFeaturesVisible(false)
        val disabled = overlay.readText()
        assertEquals(setOf("android.hardware.vulkan.compute", "android.hardware.vulkan.level",
            "android.hardware.vulkan.version", "android.software.vulkan.deqp.level"),
            Regex("""<unavailable-feature name="([^"]+)" />""").findAll(disabled).map { it.groupValues[1] }.toSet())
        runtime.setVulkanFeaturesVisible(false)
        assertEquals(disabled, overlay.readText())
        assertEquals(original, stock.readText())

        runtime.setVulkanFeaturesVisible(true)
        runtime.setVulkanFeaturesVisible(true)
        assertFalse(overlay.exists())
        assertEquals(original, stock.readText())
    }

    @Test fun exposesOpenClAndRestoresOriginalFiles() {
        val root = folder.newFolder()
        File(root, "system/lib64").mkdirs()
        File(root, "system/etc").mkdirs()
        File(root, "vendor/etc").mkdirs()
        val systemPublic = File(root, "system/etc/public.libraries.txt").apply { writeText("libvulkan.so\n") }
        val vendorPublic = File(root, "vendor/etc/public.libraries.txt").apply { writeText("libOpenCL.so\nlibother.so\n") }
        val vulkan = File(root, "system/lib64/libvulkan.so").apply { writeText("stock loader") }
        val opencl = File(root, "system/lib64/libOpenCL.so")
        val names = listOf("libvulkan.so", "libRme_gles1_enc.so", "libRme_gles2_enc.so",
            "libRmeCodecCommon.so", "libc++_shared.so", "libOpenCL.so")
        val blobs = names.associateWith { "packaged $it".encodeToByteArray() }
        val index = blobs.entries.joinToString("\n") { (name, data) ->
            val hash = MessageDigest.getInstance("SHA-256").digest(data).joinToString("") { "%02x".format(it) }
            "$name\t$hash\t${data.size}"
        }
        val runtime = TargetGpuRuntime(root, folder.newFolder()) { blobs.getValue(it.substringAfterLast('/')).inputStream() }

        runtime.prepare(index)
        runtime.prepare(index)
        assertEquals("packaged libvulkan.so", vulkan.readText())
        assertEquals("packaged libOpenCL.so", opencl.readText())
        assertTrue("libOpenCL.so" in systemPublic.readLines())
        assertFalse("libOpenCL.so" in vendorPublic.readLines())

        runtime.prepare(index, vulkan = false, openCl = true)
        assertEquals("stock loader", vulkan.readText())
        assertFalse(File(root, "system/lib64/libRme_gles2_enc.so").exists())
        assertEquals("packaged libOpenCL.so", opencl.readText())
        assertTrue("libOpenCL.so" in systemPublic.readLines())
        assertFalse("libOpenCL.so" in vendorPublic.readLines())

        runtime.prepare(index, vulkan = true, openCl = false)
        assertEquals("packaged libvulkan.so", vulkan.readText())
        assertFalse(opencl.exists())
        assertEquals("libvulkan.so\n", systemPublic.readText())
        assertEquals("libOpenCL.so\nlibother.so\n", vendorPublic.readText())

        runtime.restore()
        assertEquals("stock loader", vulkan.readText())
        assertFalse(opencl.exists())
        assertEquals("libvulkan.so\n", systemPublic.readText())
        assertEquals("libOpenCL.so\nlibother.so\n", vendorPublic.readText())
    }
}
