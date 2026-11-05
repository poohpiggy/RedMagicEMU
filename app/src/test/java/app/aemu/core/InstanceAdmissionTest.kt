package app.aemu.core

import org.junit.Assert.*
import org.junit.Test
import java.io.File
import java.nio.file.Files

class InstanceAdmissionTest {
    private val system = "REDMAGIC/NX809J/NX809J:16/BQ2A.250705.001-BP2A.250605.031.A3/20260130.143527:user/release-keys"
    private val boot = system.replace("143527", "145957")
    private fun instance(block: (File) -> Unit) {
        val images = Files.createTempDirectory("admission").toFile()
        val dir = images.resolve("abcdef")
        try {
            dir.resolve("root/system").mkdirs()
            dir.resolve("image.json").writeText("{}")
            block(dir)
        } finally { images.deleteRecursively() }
    }
    @Test fun bothExactFingerprintsAccepted() {
        InstanceAdmission.requireFingerprint(system)
        InstanceAdmission.requireFingerprint(boot)
        InstanceAdmission.requireFingerprint("  $system  ")
    }
    @Test fun emptyWhitespaceAndSingleCharacterChangeRejected() {
        for (value in listOf(null, "", " ", system.dropLast(1) + "x"))
            assertNotNull("must reject $value", runCatching { InstanceAdmission.requireFingerprint(value) }.exceptionOrNull())
    }
    @Test fun missingAndMismatchHaveDistinctReasons() {
        assertTrue(runCatching { InstanceAdmission.requireFingerprint("   ") }.exceptionOrNull()!!.message!!.contains("missing"))
        assertTrue(runCatching { InstanceAdmission.requireFingerprint("wrong") }.exceptionOrNull()!!.message!!.contains("does not match"))
    }
    @Test fun anyApprovedPropertyCanMatch() = instance { dir ->
        val prop = dir.resolve("root/system/build.prop")
        for (key in listOf("ro.build.fingerprint", "ro.system.build.fingerprint", "ro.bootimage.build.fingerprint")) {
            prop.writeText("$key=$system\n")
            assertEquals(system, InstanceAdmission.readFingerprint(dir))
        }
        prop.writeText("ro.other.fingerprint=$system\n")
        assertNull(InstanceAdmission.readFingerprint(dir))
        assertEquals(boot, InstanceAdmission.readFingerprint(dir, listOf("  $boot  ")))
        prop.delete()
        assertEquals(boot, InstanceAdmission.readFingerprint(dir, listOf(boot)))
    }
    @Test fun storedValueTakesPrecedenceOverLegacyFile() = instance { dir ->
        dir.resolve("root/system/build.prop").writeText("ro.system.build.fingerprint=$system\n")
        InstanceAdmission.requireVerified(dir, boot)
        assertNotNull(runCatching { InstanceAdmission.requireVerified(dir, "") }.exceptionOrNull())
    }
    @Test fun noValidatedFlagBypassAndRuntimeChangesIgnored() = instance { dir ->
        dir.resolve("image.json").writeText("{\"validated\":true}")
        assertNotNull(runCatching { InstanceAdmission.requireVerified(dir, null) }.exceptionOrNull())
        dir.resolve("root/system/build.prop").writeText("ro.system.build.fingerprint=$system\n")
        dir.resolve("root/system/compat-injected").writeText("runtime")
        dir.resolve("root/data").mkdirs()
        dir.resolve("root/data/userdata").writeText("changed")
        InstanceAdmission.requireVerified(dir, null)
    }
    @Test fun multipleExistingInstancesCannotStartViaAdmission() {
        val images = Files.createTempDirectory("images").toFile()
        try {
            val first = images.resolve("abcdef").apply { mkdirs() }
            first.resolve("image.json").writeText("{}")
            InstanceAdmission.requireVerified(first, system)
            val second = images.resolve("ghjkmn").apply { mkdirs() }
            second.resolve("image.json").writeText("{}")
            assertNotNull("must reject either instance when legacy data contains multiple instances",
                runCatching { InstanceAdmission.requireVerified(first, system) }.exceptionOrNull())
            assertNotNull(runCatching { InstanceAdmission.requireVerified(second, system) }.exceptionOrNull())
        } finally { images.deleteRecursively() }
    }
}
