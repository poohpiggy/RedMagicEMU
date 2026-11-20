package app.aemu.core

import org.junit.Assert.*
import org.junit.Assume.assumeNoException
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File
import java.io.IOException
import java.nio.file.Files

class InstanceResetTest {
    @get:Rule val temp = TemporaryFolder()

    @Test fun removesGuestDataAndKeepsFirmwareSharedCardAndHostSettings() {
        val instance = temp.newFolder("abcdef")
        val data = write(instance, "root/data/user/0/app/settings")
        val cache = write(instance, "root/cache/old")
        val metadata = write(instance, "root/metadata/aconfig/old")
        val owner = write(instance, "root/dhd.owners")
        val done = write(instance, FirstRingtoneInitStore.DONE_FILE)
        val firmware = write(instance, "root/system/build.prop")
        val settings = write(instance, "image.json")
        val shared = write(temp.root, "sdcard-abcdef/DCIM/photo.jpg")

        GuestDataReset.clear(instance, syncDirectory = {}) {
            assertFalse(data.exists())
            assertFalse(done.exists())
        }

        listOf(data, cache, metadata, owner, done).forEach { assertFalse(it.path, it.exists()) }
        listOf(firmware, settings, shared).forEach { assertEquals(it.path, "keep or remove", it.readText()) }
        GuestDataReset.requireComplete(instance)
    }

    @Test fun failedFinalizationBlocksBootAndCanBeRetried() {
        val instance = temp.newFolder("abcdef")
        val data = write(instance, "root/data/system/packages.xml")
        assertThrows(IOException::class.java) {
            GuestDataReset.clear(instance, syncDirectory = {}) { throw IOException("simulated interruption") }
        }
        assertFalse(data.exists())
        assertThrows(IOException::class.java) { GuestDataReset.requireComplete(instance) }
        GuestDataReset.clear(instance, syncDirectory = {}) {}
        GuestDataReset.requireComplete(instance)
    }

    @Test fun clearsDataAndPreservesFirmwareCacheAlias() {
        val instance = temp.newFolder("abcdef")
        val cacheData = write(instance, "root/data/cache/old")
        val cache = File(instance, "root/cache")
        val alias = File("data/cache")
        link(cache, alias)

        GuestDataReset.clear(instance, syncDirectory = {}) {}
        assertFalse(cacheData.exists())
        assertEquals(alias.toPath(), Files.readSymbolicLink(cache.toPath()))
        GuestDataReset.requireComplete(instance)

        GuestDataReset.clear(instance, syncDirectory = {}) {}
        assertEquals(alias.toPath(), Files.readSymbolicLink(cache.toPath()))
        GuestDataReset.requireComplete(instance)
    }

    @Test fun rejectsUnsafeTopLevelPathsBeforeClearingAnyData() {
        val instance = temp.newFolder("abcdef")
        val data = write(instance, "root/data/system/old")
        val outside = temp.newFolder("outside")
        val keep = write(outside, "keep")
        link(File(instance, "root/cache"), outside)

        assertThrows(IOException::class.java) { GuestDataReset.clear(instance, syncDirectory = {}) {} }
        assertTrue(data.isFile)
        assertTrue(keep.isFile)
        GuestDataReset.requireComplete(instance)
    }

    @Test fun rejectsNonDirectoryBeforeClearingAnyData() {
        val instance = temp.newFolder("abcdef")
        val data = write(instance, "root/data/system/old")
        val cache = write(instance, "root/cache")

        assertThrows(IOException::class.java) { GuestDataReset.clear(instance, syncDirectory = {}) {} }
        assertTrue(data.isFile)
        assertTrue(cache.isFile)
        GuestDataReset.requireComplete(instance)
    }

    @Test fun removesNestedLinksWithoutFollowingThem() {
        val instance = temp.newFolder("abcdef")
        val data = File(instance, "root/data").apply { mkdirs() }
        val outside = temp.newFolder("outside")
        val keep = write(outside, "keep")
        link(File(data, "shared"), outside)

        GuestDataReset.clear(instance, syncDirectory = {}) {}
        assertFalse(data.exists())
        assertTrue(keep.isFile)
    }

    private fun write(dir: File, relative: String) =
        File(dir, relative).apply { parentFile!!.mkdirs(); writeText("keep or remove") }

    private fun link(link: File, target: File) {
        try {
            Files.createSymbolicLink(link.toPath(), target.toPath())
        } catch (e: IOException) {
            assumeNoException(e)
        } catch (e: UnsupportedOperationException) {
            assumeNoException(e)
        }
    }
}
