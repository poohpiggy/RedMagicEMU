package app.aemu.core

import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.ByteArrayOutputStream
import java.io.IOException
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

class TargetVoldArtifactsTest {
    @get:Rule val temp = TemporaryFolder()
    private fun jar(owner: Boolean = true, version: String = "1") = ByteArrayOutputStream().let { out ->
        ZipOutputStream(out).use { z ->
            if (owner) { z.putNextEntry(ZipEntry("META-INF/redmagic-vold-owner")); z.write("RedMagicEMU vold v1\n".toByteArray()); z.closeEntry() }
            z.putNextEntry(ZipEntry("classes.dex")); z.write(("dex\n035\u0000" + version).toByteArray()); z.closeEntry()
        }
        out.toByteArray()
    }
    @Test fun installsAndUpdatesOnlyOwnedTargetJar() {
        val root = temp.newFolder(); val dir = root.resolve("system/framework").apply { mkdirs() }
        val shared = dir.resolve("aemu-stubs.jar").apply { writeText("keep") }
        assertTrue(TargetVoldArtifacts.prepare(root, 36, "NX809J", "arm64-v8a", Engine.N64) { jar() })
        val target = dir.resolve("redmagic-vold.jar")
        assertArrayEquals(jar(), target.readBytes())
        assertTrue(TargetVoldArtifacts.prepare(root, 36, "NX809J", "arm64-v8a", Engine.N64) { jar(version="2") })
        assertArrayEquals(jar(version="2"), target.readBytes()); assertEquals("keep", shared.readText())
        target.writeBytes(jar(false)); try {
            TargetVoldArtifacts.prepare(root, 36, "NX809J", "arm64-v8a", Engine.N64) { jar() }; fail()
        } catch (_: IOException) { }
        assertArrayEquals(jar(false), target.readBytes())
    }
    @Test fun otherGuestsNeverLoadAsset() {
        val root = temp.newFolder()
        for ((api, model, abi, engine) in listOf(
            listOf(35, "NX809J", "arm64-v8a", Engine.N64), listOf(36, "other", "arm64-v8a", Engine.N64),
            listOf(36, "NX809J", "other", Engine.N64), listOf(36, "NX809J", "arm64-v8a", Engine.KK))) {
            assertFalse(TargetVoldArtifacts.prepare(root, api as Int, model as String, abi as String, engine as Engine) { error("unexpected read") })
        }
    }
    @Test fun classpathAndArgumentFollowPreparation() {
        assertEquals(listOf("-Djava.class.path=/system/framework/redmagic-vold.jar:/system/framework/aemu-stubs.jar", "userstart36"), TargetVoldArtifacts.launch(true, true))
        assertEquals(listOf("-Djava.class.path=/system/framework/aemu-stubs.jar", "userstart"), TargetVoldArtifacts.launch(false, true))
        assertEquals(listOf("-Djava.class.path=/system/framework/aemu-stubs.jar"), TargetVoldArtifacts.launch(false, false))
    }
}
