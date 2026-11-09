package app.aemu.core

import android.content.ContextWrapper
import android.content.pm.ApplicationInfo
import java.io.File
import java.nio.file.Files
import org.junit.Assert.*
import org.junit.Assume.assumeFalse
import org.junit.Assume.assumeTrue
import org.junit.Test

class GuestRunnerVibrationEngineTest {
    private class TestContext : ContextWrapper(null) {
        lateinit var appInfo: ApplicationInfo
        override fun getApplicationInfo(): ApplicationInfo = appInfo
    }

    private fun <T : Any> allocate(type: Class<T>): T {
        val unsafe = Class.forName("sun.misc.Unsafe")
        val field = unsafe.getDeclaredField("theUnsafe").apply { isAccessible = true }
        return checkNotNull(type.cast(unsafe.getMethod("allocateInstance", Class::class.java).invoke(field.get(null), type)))
    }

    private fun withRunner(action: (GuestRunner, File) -> Unit) {
        val dir = Files.createTempDirectory("vibration-engine").toFile()
        try {
            val context = allocate(TestContext::class.java)
            context.appInfo = allocate(ApplicationInfo::class.java).apply { nativeLibraryDir = dir.absolutePath }
            val image = GuestImage(id = "abcdef", name = "test", release = "16", api = 36, brand = "REDMAGIC",
                model = "NX809J", skin = "AOSP", engine = Engine.N64, abi = "arm64-v8a", bootclasspath = "",
                exports = emptyMap(), dirs = emptyList(), services = emptyList(), sdcardPath = "/sdcard",
                settings = VmSettings(vsync = false), createdAt = 0)
            action(GuestRunner(VmPaths(context, image.id, File(dir, "instance")), image), dir)
        } finally { dir.deleteRecursively() }
    }

    private val service = "/vendor/bin/hw/vendor.qti.hardware.vibrator.service"

    private fun prepareEngine(dir: File): File = File(dir, "libaemu64_vibration_36.so").apply {
        writeText("fixture")
        assertTrue(setExecutable(true))
    }

    @Test fun explicitTargetUsesOnlyTheVibrationEngine() = withRunner { runner, dir ->
        val isolated = prepareEngine(dir)
        val argv = listOf(service, "arg")
        assertEquals(listOf(isolated.absolutePath, service) + argv,
            runner.cmdline(argv, experimentalVibration = true))
    }

    @Test fun normalCommandsKeepTheirExistingEngine() = withRunner { runner, dir ->
        prepareEngine(dir)
        for (prog in listOf(service, "/system/bin/sh")) {
            val argv = listOf(prog, "arg")
            assertEquals(listOf(runner.qemu.absolutePath, prog) + argv, runner.cmdline(argv))
        }
    }

    @Test fun otherImagesAndProgramsAreRejected() = withRunner { runner, dir ->
        prepareEngine(dir)
        val candidates = listOf(
            runner.img.copy(model = "other"), runner.img.copy(api = 35),
            runner.img.copy(abi = "armeabi-v7a"), runner.img.copy(engine = Engine.KK),
        )
        for (image in candidates) {
            val other = GuestRunner(runner.paths, image)
            try {
                other.cmdline(listOf(service), experimentalVibration = true)
                fail("unexpected acceptance: $image")
            } catch (_: IllegalArgumentException) { }
        }
        for (prog in listOf("/system/bin/sh", "$service/other", "/vendor/bin/hw/../hw/vendor.qti.hardware.vibrator.service")) {
            try {
                runner.cmdline(listOf(prog), experimentalVibration = true)
                fail("unexpected acceptance: $prog")
            } catch (_: IllegalArgumentException) { }
        }
    }

    @Test fun missingEngineFailsWithoutFallback() = withRunner { runner, _ ->
        try {
            runner.cmdline(listOf(service), experimentalVibration = true)
            fail("missing engine must fail")
        } catch (_: IllegalStateException) { }
    }

    @Test fun nonExecutableEngineFailsWithoutFallback() = withRunner { runner, dir ->
        val engine = prepareEngine(dir)
        assumeTrue("filesystem cannot remove executable permissions", engine.setExecutable(false, false))
        assumeFalse("filesystem does not expose executable permissions", engine.canExecute())
        try {
            runner.cmdline(listOf(service), experimentalVibration = true)
            fail("non-executable engine must fail")
        } catch (_: IllegalStateException) { }
    }
}
