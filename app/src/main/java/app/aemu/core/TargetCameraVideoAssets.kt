package app.aemu.core

import java.io.File
import java.io.InputStream
import java.nio.file.Files
import java.nio.file.StandardCopyOption.ATOMIC_MOVE
import java.nio.file.StandardCopyOption.COPY_ATTRIBUTES
import java.nio.file.StandardCopyOption.REPLACE_EXISTING

/** Selects the NX809J camera configuration before Guest services start. */
internal object TargetCameraVideoAssets {
    private const val noRawOverlay = "vendor/overlay/rme-nx809j-camera-no-raw.apk"
    private val destinations = listOf(
        "vendor/etc/media_profiles_V1_0.xml",
        "vendor/overlay/rme-nx809j-camera.apk",
    )

    fun apply(root: File, rear1080p60: Boolean, rearRaw: Boolean, asset: (String) -> InputStream) {
        val layer = if (rear1080p60) "n64-36-camera-1080p60" else "n64-36"
        val files = destinations.associateWith { destination ->
            asset("engines/$layer/${destination.replace('/', '#')}").use { it.readBytes() }
        }.toMutableMap()
        if (!rearRaw) {
            files[noRawOverlay] = asset("engines/n64-36-camera-no-raw/${noRawOverlay.replace('/', '#')}").use { it.readBytes() }
        }
        for ((destination, contents) in files) {
            val file = File(root, destination)
            if (!file.isFile || !file.readBytes().contentEquals(contents)) replace(file, contents)
        }
        if (!rear1080p60) restoreRearPreferences(root)
        if (rearRaw) Files.deleteIfExists(File(root, noRawOverlay).toPath())
        else updatePreferences(root, "com.android.camera_PRO_0.xml") { original ->
            rawEnabled.replace(original) { "${it.groupValues[1]}off${it.groupValues[2]}" }
        }
    }

    private val rearQuality = Regex("""(<string\s+name="pref_back_video_quality_key">)6(?::[0-9]+)?(</string>)""")
    private val rearResolution = Regex("""(<string\s+name="pref_back_video_quality_tab_key">)6(</string>)""")
    private val rearFps = Regex("""(<string\s+name="pref_back_video_fps_tab_key">)60(</string>)""")
    private val rawEnabled = Regex("""(<string\s+name="pref_raw_key">)on(</string>)""")

    private fun restoreRearPreferences(root: File) {
        updatePreferences(root, "com.android.camera_preferences.xml") { original ->
            if (!rearQuality.containsMatchIn(original)) original
            else rearQuality.replace(original) { "${it.groupValues[1]}5${it.groupValues[2]}" }
                .let { text -> rearResolution.replace(text) { "${it.groupValues[1]}5${it.groupValues[2]}" } }
                .let { text -> rearFps.replace(text) { "${it.groupValues[1]}30${it.groupValues[2]}" } }
        }
    }

    private fun updatePreferences(root: File, name: String, transform: (String) -> String) {
        val directory = File(root, "data/data/com.android.camera/shared_prefs")
        val canonicalRoot = root.canonicalFile.toPath()
        // SharedPreferences may recover the .bak file after an interrupted write.
        for (suffix in listOf("", ".bak")) {
            val file = File(directory, "$name$suffix")
            if (!file.isFile || !file.canonicalFile.toPath().startsWith(canonicalRoot)) continue
            val original = file.readText()
            val updated = transform(original)
            if (updated != original) replace(file, updated.toByteArray())
        }
    }

    private fun replace(file: File, contents: ByteArray) {
        val parent = checkNotNull(file.parentFile)
        check(parent.isDirectory || parent.mkdirs()) {
            "Could not create Guest camera configuration directory"
        }
        val staged = File.createTempFile(".rme-camera-", ".tmp", parent)
        try {
            if (file.isFile) Files.copy(file.toPath(), staged.toPath(), REPLACE_EXISTING, COPY_ATTRIBUTES)
            staged.writeBytes(contents)
            if (!file.exists()) staged.setReadable(true, false)
            Files.move(staged.toPath(), file.toPath(), ATOMIC_MOVE, REPLACE_EXISTING)
        } finally {
            staged.delete()
        }
    }
}
