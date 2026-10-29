package app.aemu.core

import android.graphics.Bitmap
import android.graphics.Rect
import android.net.LocalSocket
import java.io.DataInputStream
import java.io.EOFException
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.concurrent.ConcurrentHashMap

/**
 * Hardware overlays of 2.x guests (native/overlayhal): their pictures drawn over the guest's screen.
 *
 * /dev/socket/aemu_ovl, little-endian u32 messages:
 *   1 id x y w h visible   — the overlay's place on the guest screen (SurfaceFlinger, control device)
 *   2 id w h fourcc length — a frame of it, then <length> bytes (the emulator's V4L2 driver, for the data device)
 *   3 id 0 0 0 0 0         — the overlay is gone
 * GuestScreen draws every visible overlay that has a frame into its rectangle.
 */
class OverlayFeed(paths: VmPaths, private val log: (String) -> Unit) {
    class Layer { val rect = Rect(); @Volatile var visible = false; @Volatile var placed = false;  @Volatile var bmp: Bitmap? = null; var px = IntArray(0); @Volatile var tr = 0; var rot = IntArray(0) }

    private val server = UnixServer(paths.socket("aemu_ovl"), "overlay") { c -> serve(c) }

    fun start() = server.start(log)
    fun stop() { server.stop(); layers.clear(); changed() }

    private fun serve(c: LocalSocket) {
        val inp = DataInputStream(c.inputStream.buffered(1 shl 16))
        val hd = ByteArray(28)
        val bb = ByteBuffer.wrap(hd).order(ByteOrder.LITTLE_ENDIAN)
        var data = ByteArray(0)
        try {
            while (true) {
                inp.readFully(hd, 0, 4)
                when (bb.getInt(0)) {
                    4 -> {
                        inp.readFully(hd, 4, 24)
                        val id = bb.getInt(4); val t = (bb.getInt(24) shr 8) and 7
                        log("overlay $id: transform $t, not placed")
                        layers.getOrPut(id) { Layer() }.tr = t
                        changed()
                    }
                    1, 3 -> {
                        inp.readFully(hd, 4, 24)
                        val id = bb.getInt(4)
                        val type = bb.getInt(0)
                        log("overlay $id: " + if (type == 3) "gone" else "at ${bb.getInt(8)},${bb.getInt(12)} ${bb.getInt(16)}x${bb.getInt(20)} visible ${bb.getInt(24)}")
                        if (type == 3) layers.remove(id)
                        else layers.getOrPut(id) { Layer() }.apply {
                            rect.set(bb.getInt(8), bb.getInt(12), bb.getInt(8) + bb.getInt(16), bb.getInt(12) + bb.getInt(20))
                            visible = (bb.getInt(24) and 1) != 0
                            placed = true
                            tr = (bb.getInt(24) shr 8) and 7
                        }
                        changed()
                    }
                    2 -> {
                        inp.readFully(hd, 4, 20)
                        val id = bb.getInt(4); val w = bb.getInt(8); val h = bb.getInt(12); val four = bb.getInt(16); val len = bb.getInt(20)
                        if (len < 0 || len > 32 shl 20 || w <= 0 || h <= 0 || w > 4096 || h > 4096) return
                        if (data.size < len) data = ByteArray(len)
                        inp.readFully(data, 0, len)
                        show(id, data, len, w, h, four)
                    }
                    else -> return
                }
            }
        } catch (_: EOFException) {
        } catch (e: Exception) { log("overlay: ${e.message}") }
    }

    companion object {
        /** the overlays of the running guest, by id */
        val layers = ConcurrentHashMap<Int, Layer>()
        /** GuestScreen redraws when an overlay moves or gets a frame */
        @Volatile var onChange: (() -> Unit)? = null
        private fun changed() { onChange?.invoke() }
        private fun four(s: String) = s[0].code or (s[1].code shl 8) or (s[2].code shl 16) or (s[3].code shl 24)
        val NV21 = four("NV21"); private val YU12 = four("YU12"); private val YUYV = four("YUYV")
        private val NV61 = four("NV61"); private val RGBP = four("RGBP")

        private fun argb(y0: Int, u0: Int, v0: Int): Int {
            val c = (y0 - 16).coerceAtLeast(0) * 1192; val d = u0 - 128; val e = v0 - 128
            val r = ((c + 1634 * e) shr 10).coerceIn(0, 255)
            val g = ((c - 833 * e - 400 * d) shr 10).coerceIn(0, 255)
            val b = ((c + 2066 * d) shr 10).coerceIn(0, 255)
            return (0xff shl 24) or (r shl 16) or (g shl 8) or b
        }

        /** a frame for overlay <id> (from the V4L2 driver, or from CameraFeed for a camera HAL that names its overlay) */
        /** turn: HAL_TRANSFORM of the picture itself, applied when SurfaceFlinger sets none (CameraFeed: an upright
         *  preview for a camera HAL of 1.x, which has no display orientation) */
        fun show(id: Int, data: ByteArray, len: Int, w: Int, h: Int, fourcc: Int, unplaced: Boolean = false, turn: Int = 0) {
            // only for an overlay SurfaceFlinger placed: a frame late after "gone" must not bring it back. The camera
            // HAL's own overlay may come first: 1.6's SurfaceFlinger sets an overlay's position only when its layer's
            // geometry changes after the overlay was made (LayerBuffer::OverlaySource: onTransaction sets the flag
            // onVisibilityResolved waits for; the constructor clears it), so a camera started on a laid-out
            // SurfaceView never places it. Until it does, the overlay covers the screen: GuestScreen shows it only
            // where SurfaceFlinger left the screen black, which is the SurfaceView's hole
            val l = layers[id] ?: if (unplaced) layers.getOrPut(id) { Layer() } else return
            if (unplaced && !l.placed) l.visible = true
            synchronized(l) {
                if (l.px.size != w * h) l.px = IntArray(w * h)
                if (!toArgb(data, len, w, h, fourcc, l.px)) return
                val t = if (l.tr != 0) l.tr else turn
                var px = l.px; var bw = w; var bh = h
                if (t != 0) { if (l.rot.size != w * h) l.rot = IntArray(w * h); transform(l.px, w, h, t, l.rot); px = l.rot; if ((t and 4) != 0) { bw = h; bh = w } }
                var b = l.bmp
                if (b == null || b.width != bw || b.height != bh) { b = Bitmap.createBitmap(bw, bh, Bitmap.Config.ARGB_8888); l.bmp = b }
                b.setPixels(px, 0, bw, 0, 0, bw, bh)
            }
            onChange?.invoke()
        }

        /** HAL_TRANSFORM (system/core/include/system/graphics.h): flips first, then ROT_90 clockwise */
        private fun transform(s: IntArray, w: Int, h: Int, t: Int, out: IntArray) {
            val fh = (t and 1) != 0; val fv = (t and 2) != 0
            if (t and 4 == 0) {
                for (y in 0 until h) { val sy = if (fv) h - 1 - y else y
                    for (x in 0 until w) out[y * w + x] = s[sy * w + (if (fh) w - 1 - x else x)] }
                return
            }
            // output is h wide, w tall: out(x', y') = flipped(x = y', y = h - 1 - x')
            for (oy in 0 until w) { val sx0 = if (fh) w - 1 - oy else oy
                for (ox in 0 until h) { val y0 = h - 1 - ox; out[oy * h + ox] = s[(if (fv) h - 1 - y0 else y0) * w + sx0] } }
        }

        /** a frame of the V4L2 driver's formats (v4l2.c) into ARGB */
        fun toArgb(s: ByteArray, len: Int, w: Int, h: Int, fourcc: Int, out: IntArray): Boolean {
            val n = w * h
            when (fourcc) {
                NV21, NV61 -> {
                    if (len < n * 3 / 2) return false
                    val step = if (fourcc == NV61) 1 else 2
                    for (y in 0 until h) {
                        val cr = n + (y / step) * w
                        for (x in 0 until w) {
                            val ci = cr + (x and 1.inv())
                            out[y * w + x] = argb(s[y * w + x].toInt() and 0xff, s[ci + 1].toInt() and 0xff, s[ci].toInt() and 0xff)
                        }
                    }
                }
                YU12 -> {
                    if (len < n * 3 / 2) return false
                    val q = n / 4
                    for (y in 0 until h) for (x in 0 until w) {
                        val ci = (y / 2) * (w / 2) + x / 2
                        out[y * w + x] = argb(s[y * w + x].toInt() and 0xff, s[n + ci].toInt() and 0xff, s[n + q + ci].toInt() and 0xff)
                    }
                }
                YUYV -> {
                    if (len < n * 2) return false
                    for (y in 0 until h) for (x in 0 until w) {
                        val p = (y * w + (x and 1.inv())) * 2
                        out[y * w + x] = argb(s[(y * w + x) * 2].toInt() and 0xff, s[p + 1].toInt() and 0xff, s[p + 3].toInt() and 0xff)
                    }
                }
                RGBP -> {
                    if (len < n * 2) return false
                    for (i in 0 until n) {
                        val v = (s[i * 2].toInt() and 0xff) or ((s[i * 2 + 1].toInt() and 0xff) shl 8)
                        val r = (v shr 11) and 31; val g = (v shr 5) and 63; val b = v and 31
                        out[i] = (0xff shl 24) or ((r * 255 / 31) shl 16) or ((g * 255 / 63) shl 8) or (b * 255 / 31)
                    }
                }
                else -> return false
            }
            return true
        }
    }
}
