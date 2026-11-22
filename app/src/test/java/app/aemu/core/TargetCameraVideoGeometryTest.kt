package app.aemu.core

import org.junit.Assert.assertArrayEquals
import org.junit.Test

class TargetCameraVideoGeometryTest {
    @Test fun removesTheAxisSwapReportedByK80() {
        val matrix = transform(0f, -1f, -1f, 0f, 1f, 1f)

        alignCameraVideoTexture(matrix)

        assertArrayEquals(transform(1f, 0f, 0f, -1f, 0f, 1f), matrix, 0.00001f)
    }

    @Test fun oppositeQuarterTurnRetainsCropAndTextureFlip() {
        val matrix = transform(0f, 0.8f, 0.9f, 0f, 0.05f, 0.1f)

        alignCameraVideoTexture(matrix)

        assertArrayEquals(transform(0.9f, 0f, 0f, -0.8f, 0.05f, 0.9f), matrix, 0.00001f)
    }

    @Test fun keepsAnAlreadyAlignedTextureUnchanged() {
        val original = transform(0.9f, 0f, 0f, -0.8f, 0.05f, 0.9f)
        val matrix = original.copyOf()

        alignCameraVideoTexture(matrix)

        assertArrayEquals(original, matrix, 0f)
    }

    private fun transform(a: Float, b: Float, c: Float, d: Float, x: Float, y: Float) = floatArrayOf(
        a, b, 0f, 0f,
        c, d, 0f, 0f,
        0f, 0f, 1f, 0f,
        x, y, 0f, 1f,
    )
}
