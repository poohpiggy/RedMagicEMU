package app.aemu.core

/** Maps a host YUV stream to the largest centered 4:3 crop the Guest HAL can receive. */
internal data class TargetCameraPhotoPreview private constructor(
    val sourceWidth: Int,
    val sourceHeight: Int,
    val width: Int,
    val height: Int,
) {
    val left = ((sourceWidth - width) / 2) and -2
    val top = ((sourceHeight - height) / 2) and -2

    companion object {
        fun rank(sizes: List<Pair<Int, Int>>): List<TargetCameraPhotoPreview> = sizes.mapNotNull { (width, height) ->
            if (width !in CameraFeed.W..1920 || height !in CameraFeed.H..1080 || width % 2 != 0 || height % 2 != 0) {
                return@mapNotNull null
            }
            // Multiples of 8×6 keep the crop 4:3 and both dimensions even for YUV 4:2:0.
            val units = minOf(width / 8, height / 6)
            if (units * 8 <= CameraFeed.W) return@mapNotNull null
            TargetCameraPhotoPreview(width, height, units * 8, units * 6)
        }.distinct().sortedWith(compareByDescending<TargetCameraPhotoPreview> { it.width }
            .thenBy { it.sourceWidth * it.sourceHeight })
    }
}
