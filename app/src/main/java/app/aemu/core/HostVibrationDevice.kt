package app.aemu.core

import android.content.Context
import android.os.VibrationEffect
import android.os.Vibrator

/** Creates an unconnected controller for the host's ordinary vibrator API. */
internal object HostVibrationDevice {
    fun fromContext(context: Context): HostVibrationControl? =
        fromVibrator(context.getSystemService(Vibrator::class.java))

    fun fromVibrator(vibrator: Vibrator?): HostVibrationControl? {
        if (vibrator == null || !vibrator.hasVibrator()) return null
        return HostVibrationControl(
            { duration ->
                check(vibrator.hasVibrator()) { "Host vibrator unavailable" }
                vibrator.vibrate(VibrationEffect.createOneShot(duration, VibrationEffect.DEFAULT_AMPLITUDE))
            },
            { vibrator.cancel() },
        )
    }
}
