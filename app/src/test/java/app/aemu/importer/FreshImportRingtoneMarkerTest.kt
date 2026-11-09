package app.aemu.importer

import app.aemu.core.FirstRingtoneInitStore
import java.io.File
import java.io.IOException
import java.nio.file.Files
import java.nio.file.SecureDirectoryStream
import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class FreshImportRingtoneMarkerTest {
    @get:Rule val temp = TemporaryFolder()

    private fun images(): File = temp.newFolder("images").also { dir ->
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

    @Test fun markerFailurePreventsPublishing() {
        val images = images()
        PendingImports.begin(images).use { lease ->
            payload(lease)
            File(lease.dir, "abcdef/${FirstRingtoneInitStore.FRESH_FILE}").writeText("existing")
            try { publishWithFreshRingtoneMarker(lease, "abcdef") { it() }; fail("must reject existing marker") }
            catch (_: IOException) { }
            assertFalse(File(images, "abcdef").exists())
        }
    }
}
