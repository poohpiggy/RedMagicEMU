package app.aemu.core

import org.junit.Assert.*
import org.junit.Test
import org.junit.Rule
import org.junit.rules.TemporaryFolder
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference

class GuestBootMetadataTest {
    @get:Rule val temp = TemporaryFolder()

    @Test fun bootStatisticsPreserveWifiSettingsEditedDuringBoot() {
        val started = GuestImage(id = "abcdef", name = "test", release = "16", api = 36,
            brand = "REDMAGIC", model = "NX809J", skin = "AOSP", engine = Engine.N64,
            abi = "arm64-v8a", bootclasspath = "", exports = emptyMap(), dirs = emptyList(),
            services = emptyList(), sdcardPath = "/sdcard", settings = VmSettings(), createdAt = 0,
            bootCount = 2)
        val edited = started.copy(settings = started.settings.copy(
            virtualWifi = TargetVirtualWifiConfig(ssid = "Edited AP", rssiDbm = -70)), name = "renamed")
        val staleResult = started.copy(lastBootMs = 1234, bootCount = started.bootCount + 1)
        assertNotEquals(edited.settings, staleResult.settings)
        val merged = GuestBootMetadata.apply(edited, 1234)
        assertEquals(edited.settings, merged.settings)
        assertEquals("renamed", merged.name)
        assertEquals(3, merged.bootCount)
        assertEquals(1234, merged.lastBootMs)
        assertEquals(edited, merged.copy(lastBootMs = edited.lastBootMs, bootCount = edited.bootCount))
    }

    @Test fun metadataTransactionsDoNotLoseConcurrentChanges() {
        data class Metadata(val setting: String, val boots: Int)
        val images = temp.newFolder("images")
        val state = AtomicReference(Metadata("initial", 1))
        val reading = CountDownLatch(1)
        val release = CountDownLatch(1)
        val secondStarted = CountDownLatch(1)
        val secondWrote = CountDownLatch(1)
        val pool = Executors.newFixedThreadPool(2)
        try {
            val first = pool.submit {
                ImageStore.updateLocked(images, {
                    val current = state.get()
                    reading.countDown()
                    check(release.await(5, TimeUnit.SECONDS))
                    current
                }, { state.set(it) }, { it.copy(setting = "edited") })
            }
            assertTrue(reading.await(5, TimeUnit.SECONDS))
            val second = pool.submit {
                secondStarted.countDown()
                ImageStore.updateLocked(images, { state.get() }, { state.set(it); secondWrote.countDown() }, { it.copy(boots = it.boots + 1) })
            }
            assertTrue(secondStarted.await(5, TimeUnit.SECONDS))
            assertFalse("second writer passed the active transaction", secondWrote.await(250, TimeUnit.MILLISECONDS))
            release.countDown()
            first.get(5, TimeUnit.SECONDS)
            second.get(5, TimeUnit.SECONDS)
            assertEquals(Metadata("edited", 2), state.get())
        } finally {
            release.countDown()
            pool.shutdownNow()
        }
    }
}
