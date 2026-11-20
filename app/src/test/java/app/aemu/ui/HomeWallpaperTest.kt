package app.aemu.ui

import java.io.File
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertNull
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class HomeWallpaperTest {
    @get:Rule val temp = TemporaryFolder()

    @Test fun readsTheOriginalLockscreenWallpaper() {
        val instance = temp.newFolder()
        val original = byteArrayOf(1, 2, 3)
        writeTheme(instance, "wallpaper/wallpaper1.jpg" to byteArrayOf(4),
            "lockscreen/wallpaper1.jpg" to original)

        assertArrayEquals(original, readHomeWallpaper(instance))
    }

    @Test fun skipsMissingOrOversizedWallpaper() {
        val instance = temp.newFolder()
        assertNull(readHomeWallpaper(instance))

        writeTheme(instance, "lockscreen/wallpaper1.jpg" to ByteArray(2 * 1024 * 1024 + 1))
        assertNull(readHomeWallpaper(instance))
    }

    private fun writeTheme(instance: File, vararg entries: Pair<String, ByteArray>) {
        val archive = File(instance, "root/system/etc/custom_config/themes/IP_MC_W_CN/default_theme_64.zmtp")
        archive.parentFile!!.mkdirs()
        ZipOutputStream(archive.outputStream()).use { zip ->
            for ((path, bytes) in entries) {
                zip.putNextEntry(ZipEntry(path))
                zip.write(bytes)
                zip.closeEntry()
            }
        }
    }
}
