package app.aemu.ui

import android.content.Context
import android.hardware.display.DisplayManager
import android.util.DisplayMetrics
import android.view.Display
import kotlin.math.roundToInt

internal data class DisplayPreset(val width: Int, val height: Int, val density: Int)
internal data class GuestViewport(val left: Int, val top: Int, val width: Int, val height: Int)

/** Use even pixel dimensions for scaled modes, rounding only where the host dimensions require it. */
internal fun scaledDisplayPresets(base: DisplayPreset): List<DisplayPreset> {
    fun scaled(quarters: Int): DisplayPreset {
        fun dimension(value: Int) = (((value.toLong() * quarters + 4) / 8) * 2).coerceAtLeast(2).toInt()
        val density = ((base.density.toLong() * quarters + 2) / 4).coerceAtLeast(1).toInt()
        return DisplayPreset(dimension(base.width), dimension(base.height), density)
    }
    return listOf(base, scaled(3), scaled(2))
}

internal fun hostDisplayPreset(width: Int, height: Int, density: Int): DisplayPreset? =
    if (width > 0 && height > 0 && density > 0) DisplayPreset(minOf(width, height), maxOf(width, height), density) else null

@Suppress("DEPRECATION")
internal fun readHostDisplay(context: Context): DisplayPreset? {
    val display = context.getSystemService(DisplayManager::class.java)?.getDisplay(Display.DEFAULT_DISPLAY) ?: return null
    val metrics = DisplayMetrics()
    display.getRealMetrics(metrics)
    return hostDisplayPreset(metrics.widthPixels, metrics.heightPixels, metrics.densityDpi)
}

/** Fit the full guest frame into the entire window without cropping or distorting it. */
internal fun fitGuestViewport(width: Int, height: Int, guestWidth: Int, guestHeight: Int): GuestViewport? {
    if (width <= 0 || height <= 0 || guestWidth <= 0 || guestHeight <= 0) return null
    val scale = minOf(width.toDouble() / guestWidth, height.toDouble() / guestHeight)
    val w = (guestWidth * scale).roundToInt().coerceIn(1, width)
    val h = (guestHeight * scale).roundToInt().coerceIn(1, height)
    return GuestViewport((width - w) / 2, (height - h) / 2, w, h)
}
