package app.aemu.core

import org.junit.Assert.*
import org.junit.Test
import java.io.File
import java.lang.reflect.InvocationTargetException
import java.nio.file.Files

class TargetSensorsConfigTest {
    private fun prepare(root: File, api: Int = 36, model: String = "NX809J", abi: String = "arm64-v8a", engine: Engine = Engine.N64): Boolean {
        val type = try { Class.forName("app.aemu.core.TargetSensorsConfig") }
        catch (_: ClassNotFoundException) { fail("Target sensor configuration preparation is missing"); throw AssertionError() }
        val instance = type.getField("INSTANCE").get(null)
        return try {
            type.getMethod("prepare", File::class.java, Int::class.javaPrimitiveType, String::class.java, String::class.java, Engine::class.java)
                .invoke(instance, root, api, model, abi, engine) as Boolean
        } catch (e: InvocationTargetException) { throw e.targetException }
    }

    private fun tempRoot(block: (File) -> Unit) {
        val root = Files.createTempDirectory("target-sensors").toFile()
        root.resolve("system").mkdirs()
        try { block(root) } finally { root.deleteRecursively() }
    }

    @Test fun targetParksExistingConfigWithoutLosingItsContentsAndIsIdempotent() = tempRoot { root ->
        val original = root.resolve("vendor/etc/sensors/hals.conf")
        checkNotNull(original.parentFile).mkdirs()
        original.writeText("sensors.qsh.so\n")
        val backup = root.resolve("system/.aemu-parked/vendor#etc#sensors#hals.conf")
        assertTrue(prepare(root))
        assertFalse(original.exists())
        assertEquals("sensors.qsh.so\n", backup.readText())
        assertFalse(prepare(root))
        assertEquals("sensors.qsh.so\n", backup.readText())
    }

    @Test fun nonTargetIsUntouched() = tempRoot { root ->
        val original = root.resolve("vendor/etc/sensors/hals.conf")
        checkNotNull(original.parentFile).mkdirs()
        original.writeText("original")
        assertFalse(prepare(root, api = 35))
        assertFalse(prepare(root, model = "other"))
        assertFalse(prepare(root, abi = "armeabi-v7a"))
        assertFalse(prepare(root, engine = Engine.GB))
        assertEquals("original", original.readText())
    }

    @Test fun backupCollisionPreservesBothFiles() = tempRoot { root ->
        val original = root.resolve("vendor/etc/sensors/hals.conf")
        val backup = root.resolve("system/.aemu-parked/vendor#etc#sensors#hals.conf")
        checkNotNull(original.parentFile).mkdirs(); checkNotNull(backup.parentFile).mkdirs()
        original.writeText("new original"); backup.writeText("existing backup")
        try { prepare(root); fail("expected collision") } catch (_: java.io.IOException) { }
        assertEquals("new original", original.readText())
        assertEquals("existing backup", backup.readText())
    }
}
