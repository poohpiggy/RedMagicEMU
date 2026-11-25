package app.aemu.importer

import app.aemu.core.FirstRingtoneInitStore
import app.aemu.core.InstancePathSafety
import java.io.File
import java.io.IOException
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.SecureDirectoryStream
import org.junit.Assert.*
import org.junit.Assume.assumeNoException
import org.junit.Assume.assumeTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class FreshImportRingtoneMarkerTest {
    @get:Rule val temp = TemporaryFolder()

    private fun appDataDir(): Path = temp.root.toPath().resolve("storage/app.redmagicemu")

    private fun images(): File = temp.newFolder("storage", "app.redmagicemu", "files", "images").also { dir ->
        Files.newDirectoryStream(dir.toPath()).use {
            assumeTrue("anchored directory operations unavailable", it is SecureDirectoryStream<*>)
        }
    }

    private fun payload(lease: PendingImports.Lease) {
        File(lease.dir, "abcdef").apply {
            check(mkdir())
            check(File(this, "root").mkdir())
            File(this, "image.json").writeText("complete")
        }
    }

    @Test fun publishedNewInstanceCarriesFreshIdentityAndOldInstanceDoesNot() {
        val images = images()
        val old = File(images, "bcdefg").apply { check(mkdir()) }
        FirstRingtoneInitStore.open(old, "bcdefg").use {
            assertEquals(FirstRingtoneInitStore.Status.UNTRACKED, it.status())
            assertNull(it.claim())
        }
        PendingImports.begin(images).use { lease ->
            payload(lease)
            publishWithFreshRingtoneMarker(lease, "abcdef") { it() }
        }
        FirstRingtoneInitStore.open(File(images, "abcdef"), "abcdef").use {
            assertEquals(FirstRingtoneInitStore.Status.FRESH, it.status())
        }
        FirstRingtoneInitStore.open(old, "bcdefg").use {
            assertEquals(FirstRingtoneInitStore.Status.UNTRACKED, it.status())
            assertNull(it.claim())
        }
    }

    @Test fun publishesNewInstanceWhenHostAppStorageUsesAnAlias() {
        val files = temp.newFolder("storage", "app.redmagicemu", "files")
        Files.newDirectoryStream(files.toPath()).use {
            assumeTrue("anchored directory operations unavailable", it is SecureDirectoryStream<*>)
        }
        val alias = temp.root.toPath().resolve("host-app-storage")
        val storage = temp.root.toPath().resolve("storage")
        try { Files.createSymbolicLink(alias, storage) } catch (e: Exception) { assumeNoException(e) }
        val appData = alias.resolve("app.redmagicemu")
        val images = InstancePathSafety.prepareImportImagesDir(appData.resolve("files"), appData)
        assertEquals(File(files, "images"), images)
        PendingImports.begin(images).use { lease ->
            payload(lease)
            publishWithFreshRingtoneMarker(lease, "abcdef") { it() }
        }
        val instance = File(images, "abcdef")
        assertEquals("complete", File(instance, "image.json").readText())
        FirstRingtoneInitStore.open(instance, "abcdef").use {
            assertEquals(FirstRingtoneInitStore.Status.FRESH, it.status())
        }
    }

    @Test fun rejectsLinkedFilesDirectoryBeforeCreatingImagesOrStagingContent() {
        val appData = Files.createDirectories(appDataDir())
        val outside = temp.newFolder("outside").toPath()
        val files = appData.resolve("files")
        try { Files.createSymbolicLink(files, outside) } catch (e: Exception) { assumeNoException(e) }

        val error = assertThrows(IOException::class.java) {
            InstancePathSafety.prepareImportImagesDir(files, appData)
        }
        assertTrue(error.message.orEmpty().contains("files"))
        assertFalse(Files.exists(outside.resolve("images")))
    }

    @Test fun rejectsLinkedImagesDirectoryBeforeStagingContent() {
        val appData = Files.createDirectories(appDataDir())
        val files = Files.createDirectory(appData.resolve("files"))
        val outside = temp.newFolder("outside").toPath()
        try { Files.createSymbolicLink(files.resolve("images"), outside) } catch (e: Exception) { assumeNoException(e) }

        val error = assertThrows(IOException::class.java) {
            InstancePathSafety.prepareImportImagesDir(files, appData)
        }
        assertTrue(error.message.orEmpty().contains("images"))
        assertFalse(Files.exists(outside.resolve(".firmware-pending")))
    }

    @Test fun markerFailurePreventsPublishing() {
        val images = images()
        PendingImports.begin(images).use { lease ->
            payload(lease)
            File(lease.dir, "abcdef/${FirstRingtoneInitStore.FRESH_FILE}").writeText("existing")
            assertThrows(IOException::class.java) {
                publishWithFreshRingtoneMarker(lease, "abcdef") { it() }
            }
            assertFalse(File(images, "abcdef").exists())
        }
    }
}
