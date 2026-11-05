package app.aemu.core

import org.junit.Assert.*
import org.junit.Assume.assumeNoException
import org.junit.Test
import java.io.File
import java.io.IOException
import java.nio.ByteBuffer
import java.nio.channels.FileChannel
import java.nio.file.Files
import javax.xml.parsers.DocumentBuilderFactory

class TargetQtiComponentTest {
    private fun prepare(root: File, api: Int = 36, model: String = "NX809J", engine: Engine = Engine.N64) =
        TargetQtiComponent.prepare(root, api, model, engine)

    private fun tempRoot(block: (File) -> Unit) {
        val root = Files.createTempDirectory("target-qti").toFile()
        try { block(root) } finally { root.deleteRecursively() }
    }

    private fun config(root: File) = root.resolve("system/etc/sysconfig/aemu-nx809j-qti.xml")

    @Test fun targetInstallsOnlyTheBrokenServiceOverrideAndIsIdempotent() = tempRoot { root ->
        val probe = root.resolve("symlink-probe").toPath()
        try { Files.createSymbolicLink(probe, java.nio.file.Paths.get("unused")) }
        catch (e: Exception) { assumeNoException(e) }
        Files.delete(probe)
        prepare(root)
        val file = config(root)
        val xml = DocumentBuilderFactory.newInstance().newDocumentBuilder().parse(file)
        assertEquals("config", xml.documentElement.tagName)
        val overrides = xml.getElementsByTagName("component-override")
        assertEquals(1, overrides.length)
        val override = overrides.item(0) as org.w3c.dom.Element
        assertEquals("com.qti.phone", override.getAttribute("package"))
        val components = override.getElementsByTagName("component")
        assertEquals(1, components.length)
        val component = components.item(0) as org.w3c.dom.Element
        assertEquals("com.qti.phone.ExtTelephonyService", component.getAttribute("class"))
        assertEquals("false", component.getAttribute("enabled"))
        val before = file.readBytes()
        prepare(root)
        assertArrayEquals(before, file.readBytes())
    }

    @Test fun otherTargetsAreUntouched() = tempRoot { root ->
        prepare(root, 35, "NX809J")
        prepare(root, 36, "other")
        prepare(root, 36, "NX809J", Engine.GB)
        assertFalse(root.resolve("system").exists())
    }

    @Test fun existingDifferentConfigIsNotOverwritten() = tempRoot { root ->
        val file = config(root)
        checkNotNull(file.parentFile).mkdirs()
        file.writeText("keep this")
        try { prepare(root); fail("expected collision") } catch (_: IOException) { }
        assertEquals("keep this", file.readText())
    }

    @Test fun symlinkedSysconfigIsRejected() = tempRoot { root ->
        val etc = root.resolve("system/etc").apply { mkdirs() }
        val outside = root.resolve("outside").apply { mkdirs() }
        try { Files.createSymbolicLink(etc.resolve("sysconfig").toPath(), outside.toPath()) }
        catch (e: Exception) { assumeNoException(e) }
        try { prepare(root); fail("expected symlink rejection") } catch (_: IOException) { }
        assertEquals(0, outside.listFiles()!!.size)
    }

    @Test fun symlinkedConfigIsRejected() = tempRoot { root ->
        val file = config(root)
        checkNotNull(file.parentFile).mkdirs()
        val outside = root.resolve("outside.xml").apply { writeText("keep this") }
        try { Files.createSymbolicLink(file.toPath(), outside.toPath()) }
        catch (e: Exception) { assumeNoException(e) }
        try { prepare(root); fail("expected symlink rejection") } catch (_: IOException) { }
        assertEquals("keep this", outside.readText())
    }

    @Test fun interruptedWriteDoesNotPublishPartialConfig() = tempRoot { root ->
        val dir = root.resolve("sysconfig").apply { mkdir() }
        val writer: (FileChannel) -> Unit = { channel ->
            channel.write(ByteBuffer.wrap("partial".toByteArray()))
            throw IOException("injected failure")
        }
        try {
            TargetQtiComponent.publish(dir.toPath(), writer)
            fail("expected write failure")
        } catch (_: IOException) { }
        assertFalse(dir.resolve("aemu-nx809j-qti.xml").exists())
        assertEquals(0, dir.listFiles()!!.size)
    }

    @Test fun shortWriteCannotPublishAnIncompleteConfig() = tempRoot { root ->
        val dir = root.resolve("sysconfig").apply { mkdir() }
        try {
            TargetQtiComponent.publish(dir.toPath()) { channel ->
                channel.write(ByteBuffer.wrap("partial".toByteArray()))
            }
            fail("expected incomplete data rejection")
        } catch (e: IOException) {
            assertTrue("unexpected failure: $e", e.message.orEmpty().contains("Conflicting QTI component override"))
        }
        assertFalse(dir.resolve("aemu-nx809j-qti.xml").exists())
        assertEquals(0, dir.listFiles()!!.size)
    }

    @Test fun targetCreatedDuringPublicationIsNotOverwritten() = tempRoot { root ->
        val dir = root.resolve("sysconfig").apply { mkdir() }
        val target = dir.resolve("aemu-nx809j-qti.xml")
        val writer: (FileChannel) -> Unit = { channel ->
            val bytes = ByteBuffer.wrap("""<?xml version="1.0" encoding="utf-8"?>
<config>
    <component-override package="com.qti.phone">
        <component class="com.qti.phone.ExtTelephonyService" enabled="false" />
    </component-override>
</config>
""".toByteArray())
            while (bytes.hasRemaining()) channel.write(bytes)
            target.writeText("preserve")
        }
        try {
            TargetQtiComponent.publish(dir.toPath(), writer)
            fail("expected collision")
        } catch (_: IOException) { }
        assertEquals("preserve", target.readText())
        assertEquals(listOf(target.name), dir.list()!!.toList())
    }
}
