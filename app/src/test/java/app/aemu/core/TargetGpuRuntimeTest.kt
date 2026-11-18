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

        runtime.restore()
        assertEquals("stock loader", vulkan.readText())
        assertFalse(opencl.exists())
        assertEquals("libvulkan.so\n", systemPublic.readText())
        assertEquals("libOpenCL.so\nlibother.so\n", vendorPublic.readText())
    }
}
