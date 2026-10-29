package app.aemu.core

import android.net.LocalSocket
import java.io.DataInputStream
import java.io.OutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder

/** AOSP's PacketVideo MPEG-4 / H.263 encoder (native/m4venc → libaemum4v.so) */
object M4vEncoder {
    init { System.loadLibrary("aemum4v") }
    @JvmStatic external fun nInit(coding: Int, w: Int, h: Int, fps: Int, bitrate: Int): Long
    @JvmStatic external fun nHeader(h: Long): ByteArray?
    @JvmStatic external fun nEncode(h: Long, nv21: ByteArray, tsUs: Long, out: ByteArray): Int
    @JvmStatic external fun nClose(h: Long)
}

/**
 * The encoder behind the guest's OMX video encoder (native/omxvenc → /dev/socket/aemu_venc).
 * Messages are [u32 type][u32 len] + payload, little-endian:
 *   1 config (guest → host): codec (OMX_VIDEO_CODINGTYPE: 3 H.263, 4 MPEG-4), width, height, fps, bitrate, 1, color
 *   2 frame  (guest → host): ts lo, ts hi, eos, then the YUV420SP (NV21) picture
 *   3 output (host → guest): ts lo, ts hi, flags (1 sync, 2 codec config, 4 eos), then the bitstream
 *   4 done   (host → guest): every output for that frame was sent
 * Encoding is AOSP's PacketVideo encoder on the host CPU, the same on every phone: MediaCodec's MPEG-4 / H.263
 * encoders take 176x144 at most (C2SoftMpeg4Enc), and hardware ones for these formats are rare.
 */
class VideoEncoderFeed(paths: VmPaths, private val log: (String) -> Unit) {
    private val server = UnixServer(paths.socket("aemu_venc"), "venc") { c -> Session(c).run() }

    fun start() = server.start(log)
    fun stop() = server.stop()

    private inner class Session(sock: LocalSocket) {
        private val inp = DataInputStream(sock.inputStream)
        private val out: OutputStream = sock.outputStream
        private var enc = 0L
        private var header: ByteArray? = null
        private var bits = ByteArray(0)
        private var frames = 0
        private var spent = 0L

        private fun u32(): Int = Integer.reverseBytes(inp.readInt())

        fun run() {
            try {
                while (true) {
                    val type = u32()
                    val len = u32()
                    if (len < 0 || len > (16 shl 20)) { log("venc: bad length $len"); return }
                    val body = ByteArray(len)
                    inp.readFully(body)
                    val b = ByteBuffer.wrap(body).order(ByteOrder.LITTLE_ENDIAN)
                    when (type) {
                        1 -> configure(b.getInt(), b.getInt(), b.getInt(), b.getInt(), b.getInt())
                        2 -> {
                            val ts = (b.getInt().toLong() and 0xffffffffL) or (b.getInt().toLong() shl 32)
                            val eos = b.getInt() != 0
                            frame(body.copyOfRange(12, len), ts, eos)
                        }
                        else -> { log("venc: unknown message $type"); return }
                    }
                }
            } catch (_: java.io.EOFException) {
            } catch (e: Throwable) {
                log("venc: $e")
            } finally {
                if (enc != 0L) M4vEncoder.nClose(enc)
                if (frames > 0) log("venc: $frames frames, ${spent / frames / 1000} us a frame")
            }
        }

        private fun configure(coding: Int, w: Int, h: Int, fps: Int, bitrate: Int) {
            if (enc != 0L) M4vEncoder.nClose(enc)
            enc = M4vEncoder.nInit(coding, w, h, fps, bitrate)
            header = if (enc != 0L) M4vEncoder.nHeader(enc) else null
            bits = ByteArray(maxOf(w * h * 3 / 2, 64 shl 10))
            log("venc: ${if (coding == 3) "H.263" else "MPEG-4"} ${w}x$h $fps fps $bitrate bit/s" +
                if (enc == 0L) " — the encoder refused these settings" else "")
        }

        private fun frame(nv21: ByteArray, ts: Long, eos: Boolean) {
            if (enc != 0L) {
                header?.let { send(ts, 2, it, it.size); header = null }
                if (nv21.isNotEmpty()) {
                    val t0 = System.nanoTime()
                    val r = M4vEncoder.nEncode(enc, nv21, ts, bits)
                    spent += System.nanoTime() - t0; frames++
                    if (r > 0) send(ts, if (r and (1 shl 30) != 0) 1 else 0, bits, r and 0x3fffffff)
                }
                if (eos) send(ts, 4, bits, 0)
            }
            out.write(ByteBuffer.allocate(8).order(ByteOrder.LITTLE_ENDIAN).putInt(4).putInt(0).array())
            out.flush()
        }

        private fun send(ts: Long, flags: Int, data: ByteArray, n: Int) {
            val hdr = ByteBuffer.allocate(20).order(ByteOrder.LITTLE_ENDIAN)
            hdr.putInt(3).putInt(12 + n).putInt(ts.toInt()).putInt((ts ushr 32).toInt()).putInt(flags)
            out.write(hdr.array()); out.write(data, 0, n)
        }
    }
}
