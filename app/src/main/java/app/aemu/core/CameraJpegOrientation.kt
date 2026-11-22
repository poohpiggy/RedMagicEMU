package app.aemu.core

/** Rotate pixels for a still capture when the host JPEG stream is unavailable. */
internal fun rotateCameraJpegNv21(pixels: ByteArray, width: Int, height: Int, rotation: Int): ByteArray {
    if (rotation == 0) return pixels
    val oriented = ByteArray(pixels.size)
    fun rotatePlane(offset: Int, planeWidth: Int, planeHeight: Int, bytesPerSample: Int) {
        val step = when (rotation) {
            90 -> planeHeight
            180 -> -1
            else -> -planeHeight
        }
        for (row in 0 until planeHeight) {
            val first = when (rotation) {
                90 -> planeHeight - 1 - row
                180 -> (planeHeight - row) * planeWidth - 1
                else -> (planeWidth - 1) * planeHeight + row
            }
            var source = offset + row * planeWidth * bytesPerSample
            var target = offset + first * bytesPerSample
            repeat(planeWidth) {
                pixels.copyInto(oriented, target, source, source + bytesPerSample)
                source += bytesPerSample
                target += step * bytesPerSample
            }
        }
    }
    rotatePlane(0, width, height, 1)
    rotatePlane(width * height, width / 2, height / 2, 2)
    return oriented
}
