package app.aemu.core

import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit

class TargetVirtualImeiTest {
    @get:Rule val temp = TemporaryFolder()

    @Test fun createsValidStableIndependentVirtualValues() {
        val first = temp.newFolder("first")
        val second = temp.newFolder("second")
        val value = TargetVirtualImei.ensure(first, "")
        assertTrue(TargetVirtualImei.valid(value))
        assertEquals(value, TargetVirtualImei.ensure(first, ""))
        assertTrue(TargetVirtualImei.valid(TargetVirtualImei.ensure(second, "")))
    }

    @Test fun adoptsValidExistingSettingWithoutReplacingIt() {
        val dir = temp.newFolder("existing")
        val old = "490154203237518"
        assertEquals(old, TargetVirtualImei.ensure(dir, old))
        assertEquals(old, TargetVirtualImei.ensure(dir, ""))
    }

    @Test fun invalidExistingValuesFailWithoutReplacing() {
        val dir = temp.newFolder("invalid")
        val stored = File(dir, "virtual-imei")
        stored.writeText("bad")
        try { TargetVirtualImei.ensure(dir, ""); fail("invalid identity accepted") }
        catch (_: java.io.IOException) { }
        assertEquals("bad", stored.readText())
        val other = temp.newFolder("invalid-setting")
        try { TargetVirtualImei.ensure(other, "123"); fail("invalid setting accepted") }
        catch (_: java.io.IOException) { }
    }

    @Test fun conflictingExistingSettingDoesNotChangeSavedValue() {
        val dir = temp.newFolder("conflict")
        val first = TargetVirtualImei.ensure(dir, "")
        try { TargetVirtualImei.ensure(dir, "490154203237518"); fail("conflict accepted") }
        catch (_: java.io.IOException) { }
        assertEquals(first, TargetVirtualImei.ensure(dir, ""))
    }

    @Test fun guestParentLinkCannotEscapeInstance() {
        val dir = temp.newFolder("linked")
        val root = File(dir, "root").apply { mkdir() }
        val outside = temp.newFolder("outside")
        File(root, "data").mkdir()
        try { java.nio.file.Files.createSymbolicLink(File(root, "data/vendor").toPath(), outside.toPath()) }
        catch (e: Exception) { org.junit.Assume.assumeNoException(e) }
        try { TargetVirtualImei.expose(root, "490154203237518"); fail("linked parent accepted") }
        catch (_: java.io.IOException) { }
        assertTrue(outside.listFiles()!!.isEmpty())
    }

    @Test fun concurrentRequestsShareOneValue() {
        val dir = temp.newFolder("concurrent")
        val pool = Executors.newFixedThreadPool(4)
        try {
            val results = (1..8).map { pool.submit<String> { TargetVirtualImei.ensure(dir, "") } }
                .map { it.get(10, TimeUnit.SECONDS) }
            assertEquals(1, results.toSet().size)
            assertTrue(TargetVirtualImei.valid(results.first()))
        } finally { pool.shutdownNow() }
    }

    @Test fun guestCopyRemainsConfinedAndCannotReplaceExistingValue() {
        val dir = temp.newFolder("runtime")
        val root = File(dir, "root").apply { mkdir() }
        val value = TargetVirtualImei.ensure(dir, "")
        TargetVirtualImei.expose(root, value)
        val file = File(root, "data/vendor/radio/aemu-imei")
        assertEquals(value, file.readText())
        TargetVirtualImei.expose(root, value)
        file.writeText("bad")
        try { TargetVirtualImei.expose(root, value); fail("conflicting file overwritten") }
        catch (_: java.io.IOException) { }
        assertEquals("bad", file.readText())
    }

    @Test fun interruptedPublicationLeavesNoPartialIdentityAndCanRetry() {
        val dir = temp.newFolder("interrupted")
        val root = File(dir, "root").apply { mkdir() }
        val value = TargetVirtualImei.ensure(dir, "")
        try {
            TargetVirtualImei.expose(root, value) { throw java.io.IOException("simulated publication failure") }
            fail("publication unexpectedly succeeded")
        } catch (_: java.io.IOException) { }
        assertFalse(File(root, "data/vendor/radio/aemu-imei").exists())
        TargetVirtualImei.expose(root, value)
        assertEquals(value, File(root, "data/vendor/radio/aemu-imei").readText())
    }

    @Test fun publicationDoesNotReplaceAConcurrentFile() {
        val dir = temp.newFolder("publication-race")
        val root = File(dir, "root").apply { mkdir() }
        val value = TargetVirtualImei.ensure(dir, "")
        val target = File(root, "data/vendor/radio/aemu-imei")
        try {
            TargetVirtualImei.expose(root, value) { target.writeText("other") }
            fail("concurrent destination overwritten")
        } catch (_: java.io.IOException) { }
        assertEquals("other", target.readText())
    }
}
