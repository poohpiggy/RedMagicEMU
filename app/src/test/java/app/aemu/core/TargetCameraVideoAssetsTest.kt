package app.aemu.core

import java.io.File
import java.io.FileNotFoundException
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class TargetCameraVideoAssetsTest {
    @get:Rule val directory = TemporaryFolder()

    private val overlay = "vendor/overlay/rme-nx809j-camera.apk"
    private val profiles = "vendor/etc/media_profiles_V1_0.xml"
    private val preferences = "data/data/com.android.camera/shared_prefs/com.android.camera_preferences.xml"

    @Test fun selectsBothAssetsAndPreservesTheUsersChoiceUntilFallback() {
        val root = directory.newFolder()
        val original = """
            <map>
                <string name="pref_front_video_quality_key">5</string>
                <string name="pref_back_video_quality_key">6:60</string>
                <string name="pref_back_video_quality_tab_key">6</string>
                <string name="pref_back_video_fps_tab_key">60</string>
            </map>
        """.trimIndent()
        val file = File(root, preferences).also { it.parentFile.mkdirs(); it.writeText(original) }

        TargetCameraVideoAssets.apply(root, true, true) { it.byteInputStream() }
        assertEquals("engines/n64-36-camera-1080p60/vendor#overlay#rme-nx809j-camera.apk", File(root, overlay).readText())
        assertEquals("engines/n64-36-camera-1080p60/vendor#etc#media_profiles_V1_0.xml", File(root, profiles).readText())
        assertEquals(original, file.readText())

        TargetCameraVideoAssets.apply(root, false, true) { it.byteInputStream() }
        assertEquals("engines/n64-36/vendor#overlay#rme-nx809j-camera.apk", File(root, overlay).readText())
        assertEquals("engines/n64-36/vendor#etc#media_profiles_V1_0.xml", File(root, profiles).readText())
        assertEquals("""
            <map>
                <string name="pref_front_video_quality_key">5</string>
                <string name="pref_back_video_quality_key">5</string>
                <string name="pref_back_video_quality_tab_key">5</string>
                <string name="pref_back_video_fps_tab_key">30</string>
            </map>
        """.trimIndent(), file.readText())
    }

    @Test fun missingAssetLeavesBothRuntimeFilesIntact() {
        val root = directory.newFolder()
        val currentOverlay = File(root, overlay).apply { parentFile.mkdirs(); writeText("overlay") }
        val currentProfiles = File(root, profiles).apply { parentFile.mkdirs(); writeText("profiles") }

        assertThrows(FileNotFoundException::class.java) {
            TargetCameraVideoAssets.apply(root, true, true) {
                if (it.endsWith(".xml")) throw FileNotFoundException(it)
                it.byteInputStream()
            }
        }

        assertEquals("overlay", currentOverlay.readText())
        assertEquals("profiles", currentProfiles.readText())
    }

    @Test fun disablesUnsupportedStillRawIndependentlyOfTheVideoProfile() {
        val root = directory.newFolder()
        val rawOverlay = File(root, "vendor/overlay/rme-nx809j-camera-no-raw.apk")
        val prefs = File(root, "data/data/com.android.camera/shared_prefs/com.android.camera_PRO_0.xml")
        val original = """<map><string name="pref_raw_key">on</string><string name="pref_camera_pro_tint_key">4</string></map>"""
        prefs.parentFile.mkdirs()
        val backup = File(prefs.path + ".bak")
        prefs.writeText(original)
        backup.writeText(original)

        TargetCameraVideoAssets.apply(root, rear1080p60 = true, rearRaw = false) { it.byteInputStream() }
        assertEquals("engines/n64-36-camera-no-raw/vendor#overlay#rme-nx809j-camera-no-raw.apk", rawOverlay.readText())
        assertEquals("engines/n64-36-camera-1080p60/vendor#etc#media_profiles_V1_0.xml", File(root, profiles).readText())
        assertEquals(original.replace(">on<", ">off<"), prefs.readText())
        assertEquals(prefs.readText(), backup.readText())

        TargetCameraVideoAssets.apply(root, rear1080p60 = false, rearRaw = true) { it.byteInputStream() }
        assertEquals(false, rawOverlay.exists())
        assertEquals(original.replace(">on<", ">off<"), prefs.readText())
    }
}
