package app.aemu.core

import java.security.MessageDigest

/** Reconstructs the single verified 1216 x 2688 theme as straight ARGB values.
 *
 * This does not create a Bitmap, premultiply channels, encode PNG, write a
 * cache, or assert that the current Guest is using this wallpaper. Callers
 * must separately establish the Guest wallpaper identity and lifecycle.
 */
internal object TargetAodForeground {
    private const val WIDTH = 1216
    private const val HEIGHT = 2688
    private const val PIXELS = WIDTH * HEIGHT
    private const val RGB_SHA256 = "67ccd417f209d9d2904013b0158456997dd0db8f52f7e6806ed50540f421f636"
    private const val ALPHA_SHA256 = "019bfeea5f8e5109befa231f8febce04732320ff1e256e51fa28b092e6231c7a"

    /** Returns null for a wrong shape, length, or digest. The returned colors
     * are unpremultiplied, including RGB at zero alpha. Mutable inputs are
     * snapshotted before hashing so later caller mutations cannot change the
     * verified data used for the result. */
    fun reconstruct(rgb: ByteArray, alpha: ByteArray, width: Int, height: Int): IntArray? {
        if (width != WIDTH || height != HEIGHT || rgb.size != PIXELS * 3 || alpha.size != PIXELS) return null
        val rgbSnapshot = rgb.copyOf()
        val alphaSnapshot = alpha.copyOf()
        if (!hasDigest(rgbSnapshot, RGB_SHA256) || !hasDigest(alphaSnapshot, ALPHA_SHA256)) return null
        return IntArray(PIXELS) { i ->
            val j = i * 3
            packArgb(rgbSnapshot[j].toInt(), rgbSnapshot[j + 1].toInt(),
                rgbSnapshot[j + 2].toInt(), alphaSnapshot[i].toInt())
        }
    }

    /** Pack low eight bits of each supplied channel without premultiplication. */
    internal fun packArgb(red: Int, green: Int, blue: Int, alpha: Int): Int =
        ((alpha and 0xff) shl 24) or ((red and 0xff) shl 16) or
            ((green and 0xff) shl 8) or (blue and 0xff)

    private fun hasDigest(data: ByteArray, expected: String): Boolean {
        val actual = MessageDigest.getInstance("SHA-256").digest(data)
        val required = ByteArray(32) { expected.substring(it * 2, it * 2 + 2).toInt(16).toByte() }
        return MessageDigest.isEqual(actual, required)
    }
}
