package app.aemu.core

import org.junit.Assert.*
import org.junit.Assume.assumeNoException
import org.junit.Test
import java.io.File
import java.io.IOException
import java.io.ByteArrayOutputStream
import java.lang.reflect.InvocationTargetException
import java.nio.file.Files
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream
import javax.xml.parsers.DocumentBuilderFactory

class TargetRadioArtifactsTest {
    private val manifest get() = File("src/main/assets/engines/redmagic/radio-manifest.xml").readBytes()
    private fun jar(version: String, owned: Boolean = true): ByteArray {
        val data = ByteArrayOutputStream()
        ZipOutputStream(data).use { zip ->
            if (owned) {
                zip.putNextEntry(ZipEntry("META-INF/redmagic-radio-owner"))
                zip.write("RedMagicEMU radio-service v1\n".toByteArray())
                zip.closeEntry()
            }
            zip.putNextEntry(ZipEntry("classes.dex")); zip.write(version.toByteArray()); zip.closeEntry()
        }
        return data.toByteArray()
    }
    private val jar = jar("one")
    private fun assets(name: String): ByteArray = when (name) {
        "engines/redmagic/radio-service.jar" -> jar
        "engines/redmagic/radio-manifest.xml" -> manifest
        else -> error("unexpected asset $name")
    }
    private fun root(block: (File) -> Unit) {
        val dir = Files.createTempDirectory("target-radio").toFile()
        try { block(dir) } finally { dir.deleteRecursively() }
    }
    private fun prepare(root: File, enabled: Boolean = true, api: Int = 36,
                        model: String = "NX809J", abi: String = "arm64-v8a", engine: Engine = Engine.N64,
                        readAsset: (String) -> ByteArray = ::assets) {
        val type = try { Class.forName("app.aemu.core.TargetRadioArtifacts") }
        catch (missing: ClassNotFoundException) { throw AssertionError("target radio installer missing", missing) }
        val instance = type.getField("INSTANCE").get(null)
        val method = type.declaredMethods.single { it.name.startsWith("prepare") && it.parameterCount == 7 }
        method.isAccessible = true
        try { method.invoke(instance, root, api, model, abi, engine, enabled, readAsset) }
        catch (wrapped: InvocationTargetException) { throw wrapped.targetException }
    }
    private fun declaration(root: File) = root.resolve("vendor/etc/vintf/manifest/aemu-redmagic-radio.xml")
    private fun installedJar(root: File) = root.resolve("system/framework/redmagic-radio.jar")
    private fun iwlanOverride(root: File) = root.resolve("system/etc/sysconfig/aemu-redmagic-iwlan.xml")

    @Test fun targetRadioInstallsOnlyIwlanComponentDefaultOverrideAndIsIdempotent() = root { root ->
        prepare(root)
        val target = iwlanOverride(root)
        assertTrue(Files.isRegularFile(target.toPath(), java.nio.file.LinkOption.NOFOLLOW_LINKS))
        val doc = DocumentBuilderFactory.newInstance().newDocumentBuilder().parse(target)
        assertEquals("config", doc.documentElement.tagName)
        val overrides = doc.getElementsByTagName("component-override")
        assertEquals(1, overrides.length)
        val entry = overrides.item(0) as org.w3c.dom.Element
        assertEquals("vendor.qti.iwlan", entry.getAttribute("package"))
        val components = entry.getElementsByTagName("component")
        assertEquals(1, components.length)
        val component = components.item(0) as org.w3c.dom.Element
        assertEquals("vendor.qti.iwlan.IWlanNetworkService", component.getAttribute("class"))
        assertEquals("false", component.getAttribute("enabled"))
        val original = target.readBytes()
        prepare(root)
        assertArrayEquals(original, target.readBytes())
    }

    @Test fun disablingRadioRemovesOnlyOwnedIwlanOverride() = root { root ->
        prepare(root)
        assertTrue(iwlanOverride(root).isFile)
        prepare(root, enabled = false)
        assertFalse(iwlanOverride(root).exists())
        assertFalse(declaration(root).exists())
        assertArrayEquals(jar, installedJar(root).readBytes())
        prepare(root, enabled = false)
        prepare(root)
        assertTrue(iwlanOverride(root).isFile)
    }

    @Test fun foreignIwlanOverrideIsNeverReplacedOrDeleted() = root { root ->
        val target = iwlanOverride(root)
        target.parentFile!!.mkdirs()
        target.writeText("foreign")
        for (enabled in listOf(true, false)) {
            try { prepare(root, enabled); fail("expected conflicting IWLAN override") } catch (_: IOException) { }
            assertEquals("foreign", target.readText())
        }
    }

    @Test fun conflictingIwlanOverrideDoesNotRemoveRadioDeclarationOnDisable() = root { root ->
        prepare(root)
        iwlanOverride(root).writeText("foreign")
        try { prepare(root, enabled = false); fail("expected conflicting IWLAN override") } catch (_: IOException) { }
        assertEquals("foreign", iwlanOverride(root).readText())
        assertArrayEquals(manifest, declaration(root).readBytes())
        assertArrayEquals(jar, installedJar(root).readBytes())
    }

    @Test fun nonTargetAndDisabledAbsentIwlanOverrideDoNotCreateFiles() = root { root ->
        prepare(root, enabled = false)
        prepare(root, api = 35); prepare(root, model = "other")
        prepare(root, abi = "armeabi-v7a"); prepare(root, engine = Engine.GB)
        assertEquals(0, root.list()!!.size)
    }

    @Test fun symlinkedIwlanSysconfigIsRejected() = root { root ->
        val etc = root.resolve("system/etc").apply { mkdirs() }
        val outside = root.resolve("outside").apply { mkdir() }
        try { Files.createSymbolicLink(etc.resolve("sysconfig").toPath(), outside.toPath()) }
        catch (e: Exception) { assumeNoException(e) }
        try { prepare(root); fail("expected sysconfig directory symlink rejection") } catch (_: IOException) { }
        assertEquals(0, outside.listFiles()!!.size)
    }

    @Test fun installsPlainSingleSlotFragmentAndJarAndIsIdempotent() = root { root ->
        prepare(root)
        assertTrue(Files.isRegularFile(declaration(root).toPath(), java.nio.file.LinkOption.NOFOLLOW_LINKS))
        assertArrayEquals(manifest, declaration(root).readBytes())
        assertArrayEquals(jar, installedJar(root).readBytes())
        prepare(root)
        assertArrayEquals(manifest, declaration(root).readBytes())
        val doc = DocumentBuilderFactory.newInstance().newDocumentBuilder().parse(declaration(root))
        val elements = doc.getElementsByTagName("hal")
        assertEquals(7, elements.length)
        val pairs = (0 until elements.length).map { index ->
            val node = elements.item(index) as org.w3c.dom.Element
            assertEquals("aidl", node.getAttribute("format"))
            assertEquals("4", node.getElementsByTagName("version").item(0).textContent)
            node.getElementsByTagName("name").item(0).textContent to node.getElementsByTagName("fqname").item(0).textContent
        }.toSet()
        assertEquals(setOf(
            "android.hardware.radio.config" to "IRadioConfig/default",
            "android.hardware.radio.modem" to "IRadioModem/slot1",
            "android.hardware.radio.sim" to "IRadioSim/slot1",
            "android.hardware.radio.network" to "IRadioNetwork/slot1",
            "android.hardware.radio.data" to "IRadioData/slot1",
            "android.hardware.radio.messaging" to "IRadioMessaging/slot1",
            "android.hardware.radio.voice" to "IRadioVoice/slot1",
        ), pairs)
    }

    @Test fun otherDevicesAndEnginesRemainUntouched() = root { root ->
        prepare(root, api = 35); prepare(root, model = "other")
        prepare(root, abi = "armeabi-v7a"); prepare(root, engine = Engine.GB)
        assertEquals(0, root.list()!!.size)
    }

    @Test fun disablingRemovesOnlyExactDeclarationAndPreservesJar() = root { root ->
        prepare(root)
        prepare(root, enabled = false)
        assertFalse(declaration(root).exists())
        assertArrayEquals(jar, installedJar(root).readBytes())
        prepare(root, enabled = false)
        prepare(root)
        assertArrayEquals(manifest, declaration(root).readBytes())
    }

    @Test fun conflictingDeclarationIsNeverOverwrittenOrDeleted() = root { root ->
        val target = declaration(root); target.parentFile!!.mkdirs(); target.writeText("foreign declaration")
        for (enabled in listOf(true, false)) {
            try { prepare(root, enabled); fail("expected conflicting declaration") } catch (_: IOException) { }
            assertEquals("foreign declaration", target.readText())
        }
    }

    @Test fun unsafeSymlinksAreRejectedWithoutTouchingOutside() = root { root ->
        val vendor = root.resolve("vendor/etc/vintf").apply { mkdirs() }
        val outside = root.resolve("outside").apply { mkdir() }
        try { Files.createSymbolicLink(vendor.resolve("manifest").toPath(), outside.toPath()) }
        catch (e: Exception) { assumeNoException(e) }
        try { prepare(root); fail("expected directory symlink rejection") } catch (_: IOException) { }
        assertEquals(0, outside.listFiles()!!.size)
    }

    @Test fun packageOwnedJarIsUpdatedWhenAssetChanges() = root { root ->
        prepare(root)
        val replacement = jar("two")
        prepare(root, readAsset = { name ->
            if (name.endsWith("radio-service.jar")) replacement else manifest
        })
        assertArrayEquals(replacement, installedJar(root).readBytes())
    }

    @Test fun unknownJarIsNotOverwritten() = root { root ->
        val target = installedJar(root); target.parentFile!!.mkdirs()
        val foreign = jar("foreign", owned = false); target.writeBytes(foreign)
        try { prepare(root); fail("expected unowned jar collision") } catch (_: IOException) { }
        assertArrayEquals(foreign, target.readBytes())
        assertFalse(declaration(root).exists())
    }

    @Test fun missingJarAssetCannotPublishDeclaration() = root { root ->
        try {
            prepare(root, readAsset = { name ->
                if (name.endsWith("radio-service.jar")) throw IOException("missing asset") else manifest
            })
            fail("expected missing jar failure")
        } catch (e: IOException) { assertEquals("missing asset", e.message) }
        assertFalse(declaration(root).exists())
        assertFalse(installedJar(root).exists())
    }

    @Test fun directoryAtJarPathIsNotReplaced() = root { root ->
        val target = installedJar(root); target.mkdirs()
        target.resolve("keep").writeText("keep")
        try { prepare(root); fail("expected conflicting jar directory") } catch (_: IOException) { }
        assertEquals("keep", target.resolve("keep").readText())
        assertFalse(declaration(root).exists())
    }
}
