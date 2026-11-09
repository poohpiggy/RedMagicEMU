package app.aemu.core

import org.junit.Assert.*
import org.junit.Assume.assumeNoException
import org.junit.Test
import java.io.File
import java.io.IOException
import java.lang.reflect.InvocationTargetException
import java.nio.file.Files

class TargetVibrationArtifactsTest {
    private val asset = ByteArray(64).apply {
        this[0] = 0x7f; this[1] = 'E'.code.toByte(); this[2] = 'L'.code.toByte(); this[3] = 'F'.code.toByte()
        this[4] = 2; this[5] = 1; this[16] = 3; this[18] = 0xb7.toByte()
    }
    private fun prepare(root: File, enabled: Boolean = true, api: Int = 36, model: String = "NX809J",
                        abi: String = "arm64-v8a", engine: Engine = Engine.N64, bytes: ByteArray = asset) {
        val type = try { Class.forName("app.aemu.core.TargetVibrationArtifacts") }
        catch (missing: ClassNotFoundException) { throw AssertionError("vibration installer missing", missing) }
        val method = type.declaredMethods.single { it.name.startsWith("prepare") && it.parameterCount == 7 }
        method.isAccessible = true
        try { method.invoke(type.getField("INSTANCE").get(null), root, api, model, abi, engine, enabled, { _: String -> bytes }) }
        catch (wrapped: InvocationTargetException) { throw wrapped.targetException }
    }
    private fun root(block: (File) -> Unit) {
        val dir = Files.createTempDirectory("target-vibration").toFile()
        try { block(dir) } finally { dir.deleteRecursively() }
    }
    private fun installed(root: File) = root.resolve("vendor/lib64/libredmagic_vibrator.so")

    @Test fun installsForTargetAndIsIdempotent() = root { root ->
        prepare(root)
        assertArrayEquals(asset, installed(root).readBytes())
        prepare(root)
        assertArrayEquals(asset, installed(root).readBytes())
    }

    @Test fun disabledAndOtherFirmwareCreateNothing() = root { root ->
        prepare(root, enabled = false); prepare(root, api = 35); prepare(root, model = "other")
        prepare(root, abi = "armeabi-v7a"); prepare(root, engine = Engine.GB)
        assertEquals(0, root.list()!!.size)
    }

    @Test fun rejectsForeignFileAndInvalidAssetWithoutOverwriting() = root { root ->
        val target = installed(root); target.parentFile!!.mkdirs(); target.writeText("foreign")
        try { prepare(root); fail("expected conflict") } catch (_: IOException) { }
        assertEquals("foreign", target.readText())
        target.delete()
        try { prepare(root, bytes = byteArrayOf(1, 2, 3)); fail("expected invalid ELF") } catch (_: IOException) { }
        assertFalse(target.exists())
    }

    @Test fun rejectsDirectorySymlinkOutsideRuntime() = root { root ->
        val vendor = root.resolve("vendor").apply { mkdir() }
        val outside = root.resolve("outside").apply { mkdir() }
        try { Files.createSymbolicLink(vendor.resolve("lib64").toPath(), outside.toPath()) }
        catch (e: Exception) { assumeNoException(e) }
        try { prepare(root); fail("expected unsafe directory") } catch (_: IOException) { }
        assertEquals(0, outside.listFiles()!!.size)
    }
}
