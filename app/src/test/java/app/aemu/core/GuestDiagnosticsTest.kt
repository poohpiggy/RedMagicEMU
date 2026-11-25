package app.aemu.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.ByteArrayOutputStream
import java.io.File

class GuestDiagnosticsTest {
    @get:Rule val folder = TemporaryFolder()

    private fun encodeRecord(prio: Int, tag: String, msg: String): ByteArray {
        val out = ByteArrayOutputStream()
        out.write(prio)
        out.write(tag.toByteArray(Charsets.UTF_8))
        out.write(0)
        out.write(msg.toByteArray(Charsets.UTF_8))
        out.write(0)
        return out.toByteArray()
    }

    @Test fun exportDiagnosticLogIncludesCrashSystemAndCrashesFileEvenWhenMainIsFlooded() {
        val root = folder.newFolder("root")
        val bin = folder.newFolder("bin")
        val logDir = File(root, "dev/log").apply { mkdirs() }

        File(bin, "crashes.log").writeText("2026-11-25 19:12:46 zygote 137 | FATAL EXCEPTION IN SYSTEM PROCESS\n")
        File(logDir, "main").outputStream().use { out ->
            repeat(500) { out.write(encodeRecord(3, "RenderEngine", "frame $it")) }
        }
        File(logDir, "system").writeBytes(
            encodeRecord(6, "SystemServer", "BOOT FAILURE starting ActivityManagerService")
        )
        File(logDir, "crash").writeBytes(
            encodeRecord(6, "AndroidRuntime", "FATAL EXCEPTION IN SYSTEM PROCESS: main")
        )

        val lines = GuestLog.exportDiagnosticLog(root, bin)
        val joined = lines.joinToString("\n")
        assertTrue(joined.contains("=== crashes.log ==="))
        assertTrue(joined.contains("2026-11-25 19:12:46 zygote 137"))
        assertTrue(joined.contains("=== dev/log/crash ==="))
        assertTrue(joined.contains("FATAL EXCEPTION IN SYSTEM PROCESS: main"))
        assertTrue(joined.contains("=== dev/log/system ==="))
        assertTrue(joined.contains("BOOT FAILURE starting ActivityManagerService"))
        assertTrue(joined.contains("=== dev/log/main ==="))
        assertTrue(joined.contains("frame 499"))
    }

    @Test fun systemServerDeathExtractsSummaryFromCrashBufferAndFormatsZygoteExit() {
        val root = folder.newFolder("root")
        val logDir = File(root, "dev/log").apply { mkdirs() }
        File(logDir, "crash").writeBytes(
            encodeRecord(6, "AndroidRuntime", "FATAL EXCEPTION IN SYSTEM PROCESS: main java.lang.IllegalStateException: boom")
        )

        val death = GuestLog.systemServerDeath(root)
        assertNotNull(death)
        assertTrue(death!!.contains("FATAL EXCEPTION IN SYSTEM PROCESS"))
        assertTrue(death.contains("IllegalStateException: boom"))

        val withCrash = formatZygote137Detail(death)
        assertTrue(withCrash.contains("IllegalStateException: boom"))

        val withoutCrash = formatZygote137Detail(null)
        assertTrue(withoutCrash.contains("child-process"))
        assertFalse(withoutCrash.isBlank())
    }

    @Test fun systemServerDeathReturnsNullWhenNoMarkerExists() {
        val root = folder.newFolder("root")
        val logDir = File(root, "dev/log").apply { mkdirs() }
        File(logDir, "main").writeBytes(encodeRecord(3, "BootAnimation", "playing"))
        assertNull(GuestLog.systemServerDeath(root))
    }

    @Test fun systemServerDeathPreservesCausedByPastPrevious240CharCap() {
        val root = folder.newFolder("root")
        val logDir = File(root, "dev/log").apply { mkdirs() }
        val stack = buildString {
            append("FATAL EXCEPTION IN SYSTEM PROCESS: nbopt:data_center\n")
            append("java.lang.ExceptionInInitializerError\n")
            append("\tat java.lang.reflect.Constructor.newInstance0(Native Method)\n")
            append("\tat java.lang.reflect.Constructor.newInstance(Constructor.java:343)\n")
            append("\tat cn.nubia.server.cube.datacenter.xml.m.g(Unknown Source:8)\n")
            append("\tat cn.nubia.server.cube.datacenter.xml.h.a(Unknown Source:105)\n")
            append("Caused by: java.lang.NullPointerException: Attempt to get length of null array\n")
            append("\tat cn.nubia.server.cube.datacenter.xml.e.i(Unknown Source:38)\n")
        }
        File(logDir, "crash").writeBytes(encodeRecord(6, "AndroidRuntime", stack))

        val death = GuestLog.systemServerDeath(root)
        assertNotNull(death)
        assertTrue(death!!.contains("Caused by: java.lang.NullPointerException"))
        assertTrue(death.contains("cn.nubia.server.cube.datacenter.xml.e.i"))
    }

    @Test fun prepareGuestThermalSysfsCreatesReadableEmptyDirectory() {
        val root = folder.newFolder("root")
        val thermal = prepareGuestThermalSysfs(root)
        assertTrue(thermal.isDirectory)
        assertEquals(File(root, "sys/class/thermal"), thermal)
        val entries = thermal.listFiles()
        assertNotNull(entries)
        assertEquals(0, entries!!.size)
    }

    @Test fun vulkanFeatureVisibilityHidesVulkanWhenRendererFailedOrLibrariesNotPrepared() {
        assertEquals(true, guestVulkanFeaturesVisible(vulkanReady = true, librariesPrepared = true))
        assertEquals(false, guestVulkanFeaturesVisible(vulkanReady = false, librariesPrepared = true))
        assertEquals(false, guestVulkanFeaturesVisible(vulkanReady = true, librariesPrepared = false))
        assertEquals(false, guestVulkanFeaturesVisible(vulkanReady = false, librariesPrepared = false))
    }
}
