package app.aemu.core

import java.io.File
import java.io.InputStream
import java.nio.file.Files
import java.nio.file.StandardCopyOption.ATOMIC_MOVE
import java.nio.file.StandardCopyOption.COPY_ATTRIBUTES
import java.nio.file.StandardCopyOption.REPLACE_EXISTING

/** Selects the NX809J camera configuration before Guest services start. */
internal object TargetCameraVideoAssets {
    private val destinations = listOf(
        "vendor/etc/media_profiles_V1_0.xml",
        "vendor/overlay/rme-nx809j-camera.apk",
    )

    fun apply(root: File, rear1080p60: Boolean, asset: (String) -> InputStream) {
        val layer = if (rear1080p60) "n64-36-camera-1080p60" else "n64-36"
        val files = destinations.associateWith { destination ->
            asset("engines/$layer/${destination.replace('/', '#')}").use { it.readBytes() }
        }
        for ((destination, contents) in files) {
            val file = File(root, destination)
            if (!file.isFile || !file.readBytes().contentEquals(contents)) replace(file, contents)
        }
        if (!rear1080p60) restoreRearPreferences(root)
    }

    private val rearQuality = Regex("""(<string\s+name="pref_back_video_quality_key">)6(?::[0-9]+)?(</string>)""")
    private val rearResolution = Regex("""(<string\s+name="pref_back_video_quality_tab_key">)6(</string>)""")
    private val rearFps = Regex("""(<string\s+name="pref_back_video_fps_tab_key">)60(</string>)""")

    private fun restoreRearPreferences(root: File) {
        val directory = File(root, "data/data/com.android.camera/shared_prefs")
        val canonicalRoot = root.canonicalFile.toPath()
        // SharedPreferences may recover the .bak file after an interrupted write.
        for (suffix in listOf("", ".bak")) {
            val file = File(directory, "com.android.camera_preferences.xml$suffix")
            if (!file.isFile || !file.canonicalFile.toPath().startsWith(canonicalRoot)) continue
            val original = file.readText()
            if (!rearQuality.containsMatchIn(original)) continue
            val updated = rearQuality.replace(original) { "${it.groupValues[1]}5${it.groupValues[2]}" }
                .let { text -> rearResolution.replace(text) { "${it.groupValues[1]}5${it.groupValues[2]}" } }
                .let { text -> rearFps.replace(text) { "${it.groupValues[1]}30${it.groupValues[2]}" } }
            replace(file, updated.toByteArray())
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
