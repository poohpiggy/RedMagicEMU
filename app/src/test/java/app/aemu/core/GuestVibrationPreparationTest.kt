package app.aemu.core

import java.io.File
import java.nio.file.Files
import org.junit.Assert.*
import org.junit.Test

class GuestVibrationPreparationTest {
    private val asset = ByteArray(64).apply {
        this[0] = 0x7f; this[1] = 'E'.code.toByte(); this[2] = 'L'.code.toByte(); this[3] = 'F'.code.toByte()
        this[4] = 2; this[5] = 1; this[16] = 3; this[18] = 0xb7.toByte()
    }

    private fun withRoot(test: (File) -> Unit) {
        val root = Files.createTempDirectory("vibration-boot").toFile()
        try { test(root) } finally { root.deleteRecursively() }
    }

    private fun target(root: File) = root.resolve("vendor/lib64/libredmagic_vibrator.so")
    private fun prepare(root: File, recovery: Boolean, model: String = "NX809J", logs: MutableList<String>): Boolean =
        prepareVibrationRuntime(recovery, {
            TargetVibrationArtifacts.prepare(root, 36, model, "arm64-v8a", Engine.N64, true) { asset }
        }, { message: String -> logs.add(message) })

    @Test fun normalTargetBootPreparesRuntimeCopy() = withRoot { root ->
        val logs = mutableListOf<String>()
        assertEquals(true, prepare(root, false, logs = logs))
        assertArrayEquals(asset, target(root).readBytes())
        assertEquals(true, prepare(root, false, logs = logs))
        assertTrue(logs.isEmpty())
    }

    @Test fun recoveryAndOtherFirmwareDoNotPrepareTheAdapter() = withRoot { root ->
        val logs = mutableListOf<String>()
        assertEquals(false, prepare(root, true, logs = logs))
        assertEquals(false, prepare(root, false, model = "other", logs = logs))
        assertFalse(target(root).exists())
        assertTrue(logs.isEmpty())
    }

    @Test fun conflictingRuntimeFileIsPreservedAndFailureReported() = withRoot { root ->
        val existing = target(root)
        existing.parentFile!!.mkdirs()
        existing.writeText("foreign")
        val logs = mutableListOf<String>()
        assertEquals(false, prepare(root, false, logs = logs))
        assertEquals("foreign", existing.readText())
        assertEquals(1, logs.size)
        assertTrue(logs.single().contains("vibration: adapter preparation failed"))
    }
}
