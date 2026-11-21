package app.aemu.core

import android.graphics.ImageFormat
import android.graphics.Rect
import android.hardware.camera2.CameraCharacteristics as CC
import android.hardware.camera2.CaptureResult as CR
import android.media.Image
import android.util.Size
import java.nio.ByteBuffer
import java.nio.ByteOrder.LITTLE_ENDIAN

/** Describes the sensor behind the virtual RAW16 stream. */
internal data class TargetCameraRaw(val size: Size, val profile: ByteArray) {
    fun samples(image: Image): ByteArray {
        require(image.format == ImageFormat.RAW_SENSOR && image.width == size.width && image.height == size.height)
        val plane = image.planes.single()
        val rowBytes = size.width * 2
        val input = plane.buffer.duplicate()
        val base = input.position()
        require(plane.pixelStride == 2 && plane.rowStride >= rowBytes &&
            input.remaining().toLong() >= (size.height - 1L) * plane.rowStride + rowBytes)
        return ByteArray(rowBytes * size.height).also { packed ->
            for (row in 0 until size.height) {
                input.position(base + row * plane.rowStride)
                input.get(packed, row * rowBytes, rowBytes)
            }
        }
    }

    companion object {
        const val PROFILE_BYTES = 544
        const val RESULT_BYTES = 64
        private const val PROFILE_VERSION = 1

        fun forCamera(camera: CC): TargetCameraRaw? {
            if (camera.get(CC.REQUEST_AVAILABLE_CAPABILITIES)?.contains(CC.REQUEST_AVAILABLE_CAPABILITIES_RAW) != true) return null
            val map = camera.get(CC.SCALER_STREAM_CONFIGURATION_MAP) ?: return null
            val pixels = camera.get(CC.SENSOR_INFO_PIXEL_ARRAY_SIZE) ?: return null
            val active = camera.get(CC.SENSOR_INFO_ACTIVE_ARRAY_SIZE) ?: return null
            val precorrection = camera.get(CC.SENSOR_INFO_PRE_CORRECTION_ACTIVE_ARRAY_SIZE) ?: return null
            val white = camera.get(CC.SENSOR_INFO_WHITE_LEVEL)?.takeIf { it > 0 } ?: return null
            val black = camera.get(CC.SENSOR_BLACK_LEVEL_PATTERN) ?: return null
            val cfa = camera.get(CC.SENSOR_INFO_COLOR_FILTER_ARRANGEMENT)?.takeIf { it in 0..3 } ?: return null
            val illuminant = camera.get(CC.SENSOR_REFERENCE_ILLUMINANT1)?.toInt()?.takeIf { it > 0 } ?: return null
            val secondIlluminant = camera.get(CC.SENSOR_REFERENCE_ILLUMINANT2)?.toInt()?.takeIf { it > 0 } ?: 0
            val aperture = camera.get(CC.LENS_INFO_AVAILABLE_APERTURES)?.firstOrNull() ?: return null
            val focal = camera.get(CC.LENS_INFO_AVAILABLE_FOCAL_LENGTHS)?.firstOrNull() ?: return null
            val analogIso = camera.get(CC.SENSOR_MAX_ANALOG_SENSITIVITY) ?: return null
            val matrices = listOf(
                camera.get(CC.SENSOR_COLOR_TRANSFORM1), camera.get(CC.SENSOR_CALIBRATION_TRANSFORM1),
                camera.get(CC.SENSOR_FORWARD_MATRIX1), camera.get(CC.SENSOR_COLOR_TRANSFORM2),
                camera.get(CC.SENSOR_CALIBRATION_TRANSFORM2), camera.get(CC.SENSOR_FORWARD_MATRIX2),
            )
            if (matrices.take(if (secondIlluminant != 0) 6 else 3).any { it == null }) return null
            val size = map.getOutputSizes(ImageFormat.RAW_SENSOR)?.asSequence()
                ?.filter { it.width > 0 && it.width % 16 == 0 && it.height > 0 && it.height % 2 == 0 }
                ?.filter { it.width.toLong() * it.height * 2 <= 64 * 1024 * 1024 }
                ?.filter { it == pixels || it == Size(precorrection.width(), precorrection.height()) }
                ?.maxByOrNull { it.width.toLong() * it.height } ?: return null
            val out = ByteBuffer.allocate(PROFILE_BYTES).order(LITTLE_ENDIAN)
            out.putInt(size.width).putInt(size.height)
            out.putLong(map.getOutputMinFrameDuration(ImageFormat.RAW_SENSOR, size))
            out.putLong(map.getOutputStallDuration(ImageFormat.RAW_SENSOR, size))
            out.putRect(active).putRect(precorrection)
            out.putInt(pixels.width).putInt(pixels.height)
            IntArray(4).also { black.copyTo(it, 0) }.forEach { out.putInt(it) }
            out.putInt(white).putInt(analogIso).putInt(cfa).putInt(illuminant).putInt(secondIlluminant)
            out.putFloat(aperture).putFloat(focal)
            for (matrix in matrices) {
                IntArray(18).also { matrix?.copyElements(it, 0) }.forEach { out.putInt(it) }
            }
            out.putInt(PROFILE_VERSION)
            return TargetCameraRaw(size, out.array())
        }

        fun result(capture: CR): ByteArray? {
            val timestamp = capture.get(CR.SENSOR_TIMESTAMP) ?: return null
            val neutral = capture.get(CR.SENSOR_NEUTRAL_COLOR_POINT)?.takeIf { it.size == 3 } ?: return null
            val focal = capture.get(CR.LENS_FOCAL_LENGTH) ?: return null
            val aperture = capture.get(CR.LENS_APERTURE) ?: return null
            val boost = capture.get(CR.CONTROL_POST_RAW_SENSITIVITY_BOOST) ?: 100
            val black = capture.get(CR.SENSOR_DYNAMIC_BLACK_LEVEL)
            val white = capture.get(CR.SENSOR_DYNAMIC_WHITE_LEVEL) ?: 0
            val out = ByteBuffer.allocate(RESULT_BYTES).order(LITTLE_ENDIAN)
            out.putLong(timestamp).putFloat(aperture).putFloat(focal).putInt(boost).putInt(white)
            repeat(4) { out.putFloat(black?.getOrNull(it) ?: -1f) }
            neutral.forEach { out.putInt(it.numerator).putInt(it.denominator) }
            return out.array()
        }

        private fun ByteBuffer.putRect(rect: Rect) = putInt(rect.left).putInt(rect.top)
            .putInt(rect.width()).putInt(rect.height())
    }
}
