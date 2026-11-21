package app.aemu.core

import android.hardware.camera2.CameraCharacteristics as CC
import android.hardware.camera2.CaptureRequest as CR
import android.hardware.camera2.CaptureResult
import android.hardware.camera2.params.RggbChannelVector
import java.nio.ByteBuffer
import kotlin.math.pow

/** Public Camera2 controls of the physical lens backing the NX809J camera. */
internal class TargetCameraControls(private val camera: CC) {
    private val capabilities = camera.get(CC.REQUEST_AVAILABLE_CAPABILITIES) ?: intArrayOf()
    private val iso = camera.get(CC.SENSOR_INFO_SENSITIVITY_RANGE)
    private val shutter = camera.get(CC.SENSOR_INFO_EXPOSURE_TIME_RANGE)
    private val frameDuration = camera.get(CC.SENSOR_INFO_MAX_FRAME_DURATION) ?: 0L
    private val ev = camera.get(CC.CONTROL_AE_COMPENSATION_RANGE)
    private val evStep = camera.get(CC.CONTROL_AE_COMPENSATION_STEP)
    private val focus = camera.get(CC.LENS_INFO_MINIMUM_FOCUS_DISTANCE) ?: 0f
    private val afModes = camera.get(CC.CONTROL_AF_AVAILABLE_MODES) ?: intArrayOf()
    private val awbModes = camera.get(CC.CONTROL_AWB_AVAILABLE_MODES) ?: intArrayOf()
    private val manualSensor = CC.REQUEST_AVAILABLE_CAPABILITIES_MANUAL_SENSOR in capabilities &&
        iso != null && shutter != null && shutter.lower in 1L..1_000_000_000L && frameDuration >= shutter.lower
    private val manualWhiteBalance = CC.REQUEST_AVAILABLE_CAPABILITIES_MANUAL_POST_PROCESSING in capabilities
    private val exposureLimit = if (manualSensor) minOf(checkNotNull(shutter).upper, frameDuration, 1_000_000_000L) else 0L

    fun writeCaps(out: ByteBuffer) {
        out.putLong(if (manualSensor) checkNotNull(shutter).lower else 0L)
        out.putLong(exposureLimit)
        out.putLong(if (manualSensor) frameDuration else 0L)
        out.putInt(if (manualSensor) checkNotNull(iso).lower else 0)
        out.putInt(if (manualSensor) checkNotNull(iso).upper else 0)
        out.putInt(ev?.lower ?: 0).putInt(ev?.upper ?: 0)
        out.putInt(evStep?.numerator ?: 0).putInt(evStep?.denominator ?: 1)
        out.putFloat(if (CR.CONTROL_AF_MODE_OFF in afModes) focus else 0f)
        out.putInt((if (manualSensor) SENSOR else 0) or (if (manualWhiteBalance) WHITE_BALANCE else 0))
        out.putInt(afModes.modeMask()).putInt(awbModes.modeMask())
    }

    private fun IntArray.modeMask() = fold(0) { mask, mode ->
        if (mode in 0..30) mask or (1 shl mode) else mask
    }

    fun apply(request: CR.Builder, controls: Request, autoExposure: CaptureResult?, autoWhiteBalance: CaptureResult?, tint: Int) {
        if (manualSensor && (controls.iso > 0 || controls.exposure > 0)) {
            val sensitivity = checkNotNull(iso)
            val exposure = checkNotNull(shutter)
            val isoValue = (controls.iso.takeIf { it > 0 } ?: autoExposure?.get(CaptureResult.SENSOR_SENSITIVITY) ?: 100)
                .coerceIn(sensitivity.lower, sensitivity.upper)
            val exposureValue = (controls.exposure.takeIf { it > 0 } ?: autoExposure?.get(CaptureResult.SENSOR_EXPOSURE_TIME) ?: 33_333_333L)
                .coerceIn(exposure.lower, exposureLimit)
            request.set(CR.CONTROL_AE_MODE, CR.CONTROL_AE_MODE_OFF)
            request.set(CR.SENSOR_SENSITIVITY, isoValue)
            request.set(CR.SENSOR_EXPOSURE_TIME, exposureValue)
            request.set(CR.SENSOR_FRAME_DURATION, maxOf(exposureValue, minOf(33_333_333L, frameDuration)))
        } else if (ev != null && evStep != null && evStep.numerator > 0 && evStep.denominator > 0) {
            request.set(CR.CONTROL_AE_EXPOSURE_COMPENSATION, controls.ev.coerceIn(ev.lower, ev.upper))
        }
        if (controls.af in afModes) {
            request.set(CR.CONTROL_AF_MODE, controls.af)
            if (focus > 0f && controls.af == CR.CONTROL_AF_MODE_OFF)
                request.set(CR.LENS_FOCUS_DISTANCE, controls.focus.coerceIn(0f, focus))
        }
        val reference = controls.gains ?: autoWhiteBalance?.get(CaptureResult.COLOR_CORRECTION_GAINS)
        val gains = if (reference != null && tint != 0) {
            // Positive tint adds magenta by reducing green relative to red and blue.
            val green = 2f.pow(-tint.coerceIn(-150, 150) / 300f)
            normalizedGains(listOf(reference.red, reference.greenEven * green, reference.greenOdd * green, reference.blue))
        } else controls.gains
        if (manualWhiteBalance && gains != null && CR.CONTROL_AWB_MODE_OFF in awbModes) {
            request.set(CR.CONTROL_AWB_MODE, CR.CONTROL_AWB_MODE_OFF)
            request.set(CR.COLOR_CORRECTION_MODE, CR.COLOR_CORRECTION_MODE_TRANSFORM_MATRIX)
            request.set(CR.COLOR_CORRECTION_GAINS, gains)
            autoWhiteBalance?.get(CaptureResult.COLOR_CORRECTION_TRANSFORM)?.let { request.set(CR.COLOR_CORRECTION_TRANSFORM, it) }
        } else if (controls.awb in awbModes && controls.awb != CR.CONTROL_AWB_MODE_OFF) {
            request.set(CR.CONTROL_AWB_MODE, controls.awb)
        }
    }

    data class Request(val ev: Int, val iso: Int, val exposure: Long, val af: Int, val focus: Float,
                       val awb: Int, val gains: RggbChannelVector?) {
        companion object {
            fun parse(fields: List<String>): Request? {
                if (fields.size != 11) return null
                val ev = fields[1].toIntOrNull() ?: return null
                val iso = fields[2].toIntOrNull() ?: return null
                val exposure = fields[3].toLongOrNull() ?: return null
                val af = fields[4].toIntOrNull() ?: return null
                val focus = fields[5].toFloatOrNull()?.takeIf { it.isFinite() } ?: return null
                val awb = fields[6].toIntOrNull() ?: return null
                val gains = fields.subList(7, 11).map { it.toFloatOrNull()?.takeIf { value -> value.isFinite() } ?: return null }
                return Request(ev, iso, exposure, af, focus, awb, normalizedGains(gains))
            }
        }
    }

    companion object {
        const val CAP_BYTES = 64
        private const val SENSOR = 1
        private const val WHITE_BALANCE = 2

        private fun normalizedGains(gains: List<Float>): RggbChannelVector? {
            val minimum = gains.minOrNull() ?: return null
            if (minimum <= 0f || gains.any { it / minimum > 32f }) return null
            return RggbChannelVector(gains[0] / minimum, gains[1] / minimum, gains[2] / minimum, gains[3] / minimum)
        }
    }
}
