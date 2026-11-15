package app.aemu.core

import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertNull
import org.junit.Assume.assumeTrue
import org.junit.Test

class TargetAodForegroundTest {
    @Test fun packsStraightArgbEvenWhenTransparent() {
        assertEquals(0x00123456, TargetAodForeground.packArgb(0x12, 0x34, 0x56, 0))
        assertEquals(0x80123456.toInt(), TargetAodForeground.packArgb(0x12, 0x34, 0x56, 128))
        assertEquals(0xfffedcba.toInt(), TargetAodForeground.packArgb(0xfe, 0xdc, 0xba, 255))
        assertEquals(-1, TargetAodForeground.packArgb(-1, -1, -1, -1))
    }

    @Test fun rejectsWrongSizesDimensionsAndUnverifiedContent() {
        val count = 1216 * 2688
        val rgb = ByteArray(count * 3)
        val alpha = ByteArray(count)
        assertNull(TargetAodForeground.reconstruct(rgb, alpha, 1215, 2688))
        assertNull(TargetAodForeground.reconstruct(rgb, alpha, Int.MAX_VALUE, Int.MAX_VALUE))
        assertNull(TargetAodForeground.reconstruct(ByteArray(2), alpha, 1216, 2688))
        assertNull(TargetAodForeground.reconstruct(rgb, ByteArray(2), 1216, 2688))
        assertNull(TargetAodForeground.reconstruct(rgb, alpha, 1216, 2688))
    }

    @Test fun originalSampleMatchesStraightPngPixels() {
        // Explicit opt-in; copyrighted/user-derived image files stay in local-data.
        val ref = System.getenv("RME_AOD_REFERENCE_DIR")
        assumeTrue("Set RME_AOD_REFERENCE_DIR to the ignored local sample", !ref.isNullOrEmpty())
        val dir = File(ref!!)
        val rgb = File(dir, "source.rgb").readBytes()
        // Android's compile classpath omits java.desktop; the host JVM has it.
        val imageIo = Class.forName("javax.imageio.ImageIO")
        val image = imageIo.getMethod("read", File::class.java).invoke(null, File(dir, "foreground.png"))
        val type = image.javaClass
        assertEquals(1216, type.getMethod("getWidth").invoke(image))
        assertEquals(2688, type.getMethod("getHeight").invoke(image))
        val png = type.getMethod("getRGB", Int::class.javaPrimitiveType, Int::class.javaPrimitiveType,
            Int::class.javaPrimitiveType, Int::class.javaPrimitiveType, IntArray::class.java,
            Int::class.javaPrimitiveType, Int::class.javaPrimitiveType)
            .invoke(image, 0, 0, 1216, 2688, null, 0, 1216) as IntArray
        val alpha = ByteArray(png.size) { (png[it] ushr 24).toByte() }
        val actual = TargetAodForeground.reconstruct(rgb, alpha, 1216, 2688)
        requireNotNull(actual)
        assertEquals(png.size, actual.size)
        assertArrayEquals(png, actual)
        rgb[0] = (rgb[0].toInt() xor 1).toByte()
        assertNull(TargetAodForeground.reconstruct(rgb, alpha, 1216, 2688))
        rgb[0] = (rgb[0].toInt() xor 1).toByte()
        alpha[0] = (alpha[0].toInt() xor 1).toByte()
        assertNull(TargetAodForeground.reconstruct(rgb, alpha, 1216, 2688))
    }

    @Test fun projectMaskSamplesMatchOriginalForegroundAlpha() {
        val ref = System.getenv("RME_AOD_REFERENCE_DIR")
        val maskPath = System.getenv("RME_AOD_ALPHA_PNG")
        assumeTrue("Set both local reference and project mask paths", !ref.isNullOrEmpty() && !maskPath.isNullOrEmpty())
        // Read the PNG's actual grayscale samples, not color-managed getRGB values.
        val imageIo = Class.forName("javax.imageio.ImageIO")
        val read = imageIo.getMethod("read", File::class.java)
        val foreground = read.invoke(null, File(ref!!, "foreground.png"))
        val mask = read.invoke(null, File(maskPath!!))
        val imageType = Class.forName("java.awt.image.BufferedImage")
        assertEquals(1216, imageType.getMethod("getWidth").invoke(mask))
        assertEquals(2688, imageType.getMethod("getHeight").invoke(mask))
        val raster = imageType.getMethod("getRaster").invoke(mask)
        val rasterType = Class.forName("java.awt.image.Raster")
        assertEquals(1, rasterType.getMethod("getNumBands").invoke(raster))
        val samples = rasterType.getMethod("getSamples", Int::class.javaPrimitiveType,
            Int::class.javaPrimitiveType, Int::class.javaPrimitiveType, Int::class.javaPrimitiveType,
            Int::class.javaPrimitiveType, IntArray::class.java)
            .invoke(raster, 0, 0, 1216, 2688, 0, null) as IntArray
        val pixels = imageType.getMethod("getRGB", Int::class.javaPrimitiveType, Int::class.javaPrimitiveType,
            Int::class.javaPrimitiveType, Int::class.javaPrimitiveType, IntArray::class.java,
            Int::class.javaPrimitiveType, Int::class.javaPrimitiveType)
            .invoke(foreground, 0, 0, 1216, 2688, null, 0, 1216) as IntArray
        assertArrayEquals(ByteArray(pixels.size) { (pixels[it] ushr 24).toByte() },
            ByteArray(samples.size) { samples[it].toByte() })
    }
}
