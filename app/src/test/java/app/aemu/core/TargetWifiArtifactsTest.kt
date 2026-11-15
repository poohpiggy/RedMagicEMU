package app.aemu.core

import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.ByteArrayOutputStream
import java.io.IOException
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

class TargetWifiArtifactsTest {
    @get:Rule val temp = TemporaryFolder()

    @Test fun targetWifiIsEnabledAtRuntime() {
        assertTrue(TargetWifiArtifacts.RUNTIME_READY)
    }

    private fun jar(version: String, owned: Boolean = true, network: Boolean = false): ByteArray = ByteArrayOutputStream().let { bytes ->
        ZipOutputStream(bytes).use { zip ->
            if (owned) {
                zip.putNextEntry(ZipEntry(if (network) "META-INF/redmagic-wifi-net-owner" else "META-INF/redmagic-wifi-owner"))
                zip.write((if (network) "RedMagicEMU wifi network prototype v1\n" else "RedMagicEMU wifi prototype v1\n").toByteArray())
                zip.closeEntry()
            }
            zip.putNextEntry(ZipEntry("classes.dex")); zip.write(("dex\n035\u0000" + version).toByteArray()); zip.closeEntry()
        }
        bytes.toByteArray()
    }

    @Test fun networkAssetUsesItsOwnOwnershipAndLeavesMainJarUntouched() {
        val root = temp.newFolder("network")
        val framework = root.resolve("system/framework").apply { assertTrue(mkdirs()) }
        val main = framework.resolve("redmagic-wifi.jar").apply { writeBytes(jar("main")) }
        val first = jar("net-first", network = true)
        TargetWifiArtifacts.prepareNetwork(root, 36, "NX809J", "arm64-v8a", Engine.N64, true) { first }
        val target = framework.resolve("redmagic-wifi-net.jar")
        assertArrayEquals(first, target.readBytes())
        val second = jar("net-second", network = true)
        TargetWifiArtifacts.prepareNetwork(root, 36, "NX809J", "arm64-v8a", Engine.N64, true) { second }
        assertArrayEquals(second, target.readBytes())
        assertArrayEquals(jar("main"), main.readBytes())
        target.writeBytes(jar("foreign"))
        try { TargetWifiArtifacts.prepareNetwork(root, 36, "NX809J", "arm64-v8a", Engine.N64, true) { first }; fail() }
        catch (_: IOException) { }
        assertArrayEquals(jar("foreign"), target.readBytes())
    }

    @Test fun disabledOrOtherRomDoesNotReadAssetsOrChangeFiles() {
        val root = temp.newFolder("off")
        TargetWifiArtifacts.prepare(root, 36, "NX809J", "arm64-v8a", Engine.N64, false) { error("must not read") }
        TargetWifiArtifacts.prepare(root, 35, "NX809J", "arm64-v8a", Engine.N64, true) { error("must not read") }
        TargetWifiArtifacts.prepare(root, 36, "other", "arm64-v8a", Engine.N64, true) { error("must not read") }
        TargetWifiArtifacts.prepare(root, 36, "NX809J", "armeabi-v7a", Engine.N64, true) { error("must not read") }
        TargetWifiArtifacts.prepare(root, 36, "NX809J", "arm64-v8a", Engine.KK, true) { error("must not read") }
        assertTrue(root.listFiles()!!.isEmpty())
    }

    @Test fun unavailableWifiAssetLeavesExistingNetworkFilesUntouched() {
        val root = temp.newFolder("fallback")
        root.resolve("system/framework").mkdirs()
        val ethernet = root.resolve("system/framework/aemu-stubs.jar").apply { writeText("existing ethernet service") }
        try { TargetWifiArtifacts.prepare(root, 36, "NX809J", "arm64-v8a", Engine.N64, true) { throw IOException("missing") }; fail() }
        catch (_: IOException) { }
        assertEquals("existing ethernet service", ethernet.readText())
        assertFalse(root.resolve("system/framework/redmagic-wifi.jar").exists())
    }

    @Test fun ownedAssetInstallsAndUpdatesOnlyItsOwnJar() {
        val root = temp.newFolder("owned")
        val framework = root.resolve("system/framework").apply { assertTrue(mkdirs()) }
        val first = jar("first")
        TargetWifiArtifacts.prepare(root, 36, "NX809J", "arm64-v8a", Engine.N64, true) { first }
        val target = framework.resolve("redmagic-wifi.jar")
        assertArrayEquals(first, target.readBytes())
        val second = jar("second")
        TargetWifiArtifacts.prepare(root, 36, "NX809J", "arm64-v8a", Engine.N64, true) { second }
        assertArrayEquals(second, target.readBytes())
    }

    @Test fun foreignTargetAndInvalidAssetAreNeverOverwritten() {
        val root = temp.newFolder("foreign")
        val framework = root.resolve("system/framework").apply { assertTrue(mkdirs()) }
        val target = framework.resolve("redmagic-wifi.jar")
        val foreign = jar("foreign", false)
        target.writeBytes(foreign)
        try { TargetWifiArtifacts.prepare(root, 36, "NX809J", "arm64-v8a", Engine.N64, true) { jar("new") }; fail() }
        catch (_: IOException) { }
        assertArrayEquals(foreign, target.readBytes())
        target.delete()
        try { TargetWifiArtifacts.prepare(root, 36, "NX809J", "arm64-v8a", Engine.N64, true) { jar("new", false) }; fail() }
        catch (_: IOException) { }
        assertFalse(target.exists())
    }
}
