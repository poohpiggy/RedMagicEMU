package app.aemu.core

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertSame
import org.junit.Test

class CameraJpegOrientationTest {
    private val nv21 = byteArrayOf(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11)

    @Test fun fallbackJpegPixelsRotateClockwiseWithoutSwappingChroma() {
        assertArrayEquals(byteArrayOf(4, 0, 5, 1, 6, 2, 7, 3, 8, 9, 10, 11),
            rotateCameraJpegNv21(nv21, 4, 2, 90))
        assertArrayEquals(byteArrayOf(3, 7, 2, 6, 1, 5, 0, 4, 10, 11, 8, 9),
            rotateCameraJpegNv21(nv21, 4, 2, 270))
        assertArrayEquals(byteArrayOf(7, 6, 5, 4, 3, 2, 1, 0, 10, 11, 8, 9),
            rotateCameraJpegNv21(nv21, 4, 2, 180))
        assertSame(nv21, rotateCameraJpegNv21(nv21, 4, 2, 0))
    }
}
