package app.aemu.core

import android.content.ContextWrapper
import android.content.pm.ApplicationInfo
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.InputStream
import java.io.OutputStream
import java.nio.file.Files
import java.util.concurrent.TimeUnit
import org.junit.Assert.*
import org.junit.Test

class GuestRunnerBoundedTest {
    private class TestContext : ContextWrapper(null) {
        lateinit var appInfo: ApplicationInfo
        override fun getApplicationInfo(): ApplicationInfo = appInfo
    }

    private class FakeProcess(output: ByteArray, private val waits: ArrayDeque<Boolean>) : Process() {
        private val input = ByteArrayInputStream(output)
        var terminated = false
        override fun getInputStream(): InputStream = input
        override fun getErrorStream(): InputStream = ByteArrayInputStream(byteArrayOf())
        override fun getOutputStream(): OutputStream = ByteArrayOutputStream()
        override fun waitFor(): Int = 0
        override fun waitFor(timeout: Long, unit: TimeUnit): Boolean = waits.removeFirstOrNull() ?: false
        override fun exitValue(): Int = 0
        override fun destroy() { terminated = true }
        override fun destroyForcibly(): Process { terminated = true; return this }
    }

    private fun <T : Any> allocate(type: Class<T>): T {
        val unsafe = Class.forName("sun.misc.Unsafe")
        val field = unsafe.getDeclaredField("theUnsafe").apply { isAccessible = true }
        return checkNotNull(type.cast(unsafe.getMethod("allocateInstance", Class::class.java).invoke(field.get(null), type)))
    }

    private fun withRunner(firmwareExports: Map<String, String> = emptyMap(), action: (GuestRunner, File) -> Unit) {
        val dir = Files.createTempDirectory("guest-bounded").toFile()
        try {
            val context = allocate(TestContext::class.java)
            context.appInfo = allocate(ApplicationInfo::class.java).apply { nativeLibraryDir = dir.absolutePath }
            val image = GuestImage(id = "abcdef", name = "test", release = "16", api = 36, brand = "REDMAGIC",
                model = "NX809J", skin = "AOSP", engine = Engine.N64, abi = "arm64-v8a", bootclasspath = "",
                exports = firmwareExports, dirs = emptyList(), services = emptyList(), sdcardPath = "/sdcard",
                settings = VmSettings(vsync = false), createdAt = 0)
            val runner = GuestRunner(VmPaths(context, image.id, File(dir, "instance")), image)
            runner.paths.bin.mkdirs()
            runner.qemu.writeText("fixture")
            assertTrue(runner.qemu.setExecutable(true))
            action(runner, dir)
        } finally { dir.deleteRecursively() }
    }

    @Test fun usesGuestCommandEnvironmentAndTheSuppliedProcessStarter() = withRunner { runner, _ ->
        var starts = 0
        runner.processStarter = { builder ->
            starts++
            assertEquals(runner.cmdline(listOf("/system/bin/sh", "-c", "true")), builder.command())
            assertEquals(runner.paths.bin, builder.directory())
            assertTrue(builder.redirectErrorStream())
            assertEquals(runner.paths.root.absolutePath, builder.environment()["AEMU64_ROOT"])
            assertEquals("test-value", builder.environment()["TEST_EXTRA"])
            FakeProcess("complete\n".toByteArray(), ArrayDeque(listOf(true)))
        }
        assertEquals(BoundedGuestCommand.Result.Completed(0, "complete\n"),
            runner.runBounded(listOf("/system/bin/sh", "-c", "true"), 100, mapOf("TEST_EXTRA" to "test-value")))
        assertEquals(1, starts)
    }

    @Test fun explicitVibrationConnectionParametersWinOverDerivedExports() = withRunner { runner, _ ->
        val vibration = mapOf(
            "AEMU64_VIB_PRELOAD" to "1",
            "RME_VIBRATION_SOCKET" to "host-socket",
            "RME_VIBRATION_HOST_PID" to "123",
            "RME_VIBRATION_HOST_UID" to "456",
            "RME_VIBRATION_EPOCH" to "789",
        )
        runner.derivedExports = vibration.mapValues { "firmware-value" } + ("NORMAL_EXPORT" to "derived")
        runner.userQemuArgs = "RME_VIBRATION_SOCKET=user-value"
        val actual = runner.env(vibration + ("NORMAL_EXPORT" to "extra"))
        vibration.forEach { (key, value) -> assertEquals(key, value, actual[key]) }
        assertEquals("derived", actual["NORMAL_EXPORT"])
    }

    @Test fun firmwareCannotEnableVibrationWithoutExplicitParameters() = withRunner(
        mapOf("AEMU64_VIB_PRELOAD" to "1", "RME_VIBRATION_HOST_PID" to "11"),
    ) { runner, _ ->
        runner.userQemuArgs = "RME_VIBRATION_HOST_UID=12"
        runner.derivedExports = mapOf("RME_VIBRATION_SOCKET" to "derived", "RME_VIBRATION_EPOCH" to "13",
            "NORMAL_EXPORT" to "derived")
        val actual = runner.env()
        listOf("AEMU64_VIB_PRELOAD", "RME_VIBRATION_SOCKET", "RME_VIBRATION_HOST_PID",
            "RME_VIBRATION_HOST_UID", "RME_VIBRATION_EPOCH").forEach { assertFalse(it, actual.containsKey(it)) }
        assertEquals("derived", actual["NORMAL_EXPORT"])
    }

    @Test fun timeoutIsNotConvertedToAZeroExit() = withRunner { runner, _ ->
        val process = FakeProcess("ok".toByteArray(), ArrayDeque(listOf(false, true)))
        runner.processStarter = { process }
        val result = runner.runBounded(listOf("/system/bin/sh"), 100) as BoundedGuestCommand.Result.Uncertain
        assertEquals(BoundedGuestCommand.Reason.TIMEOUT, result.reason)
        assertTrue(process.terminated)
    }

    @Test fun oversizedOutputIsNotReportedAsComplete() = withRunner { runner, _ ->
        runner.processStarter = { FakeProcess(ByteArray(65_537) { 65 }, ArrayDeque(listOf(true))) }
        val result = runner.runBounded(listOf("/system/bin/sh"), 100) as BoundedGuestCommand.Result.Uncertain
        assertEquals(BoundedGuestCommand.Reason.OUTPUT_TRUNCATED, result.reason)
    }

    @Test fun absentEngineNeverStartsAProcess() = withRunner { runner, _ ->
        assertTrue(runner.qemu.delete())
        runner.processStarter = { fail("Unexpected process start"); throw AssertionError() }
        val result = runner.runBounded(listOf("/system/bin/sh"), 100) as BoundedGuestCommand.Result.Uncertain
        assertEquals(BoundedGuestCommand.Reason.START_FAILURE, result.reason)
    }
}
