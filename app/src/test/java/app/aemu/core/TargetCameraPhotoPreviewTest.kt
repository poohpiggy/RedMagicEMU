package app.aemu.core

import org.junit.Assert.assertEquals
import org.junit.Test

class TargetCameraPhotoPreviewTest {
    @Test fun wideHostStreamsKeepTheirAvailableVerticalDetail() {
        val profiles = TargetCameraPhotoPreview.rank(listOf(1280 to 720, 640 to 480, 1920 to 1080))

        assertEquals(listOf(1920 to 1080, 1280 to 720), profiles.map { it.sourceWidth to it.sourceHeight })
        assertEquals(listOf(1440 to 1080, 960 to 720), profiles.map { it.width to it.height })
        assertEquals(listOf(240 to 0, 160 to 0), profiles.map { it.left to it.top })
    }

    @Test fun nativeFourByThreeWinsAtTheSameOutputSize() {
        val profiles = TargetCameraPhotoPreview.rank(listOf(
            1920 to 1080, 1440 to 1080, 1440 to 1080, 1920 to 1081, 3840 to 2160, 640 to 480,
        ))

        assertEquals(listOf(1440, 1920), profiles.map { it.sourceWidth })
        assertEquals(listOf(0, 240), profiles.map { it.left })
    }
}
