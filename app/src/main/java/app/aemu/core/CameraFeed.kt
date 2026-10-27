package app.aemu.core

import android.annotation.SuppressLint
import android.content.Context
import android.content.pm.PackageManager
import android.graphics.ImageFormat
import android.graphics.Rect
import android.graphics.YuvImage
import android.hardware.camera2.CameraCaptureSession
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CameraDevice
import android.hardware.camera2.CameraManager
import android.media.Image
import android.media.ImageReader
import android.net.LocalSocket
import android.os.Handler
import android.os.HandlerThread
import java.io.ByteArrayOutputStream
import java.io.OutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * The phone's cameras for the guest. The emulator's camera HAL (native/camerahal) connects to /dev/socket/aemu_cam
 * and sends "open <0 back | 1 front>", "start", "stop", "shot", "close"; frames go back as
 *     int32 type, width, height, length + data — type 1: RGBA_8888 then NV21 (preview), type 2: JPEG (picture),
 *     type 3: NV21 only (preview, after "nv21").
 * "zoom <ratio×100>": digital zoom of every frame (preview, recording and picture).
 * "size <w> <h>": NV21 previews of that size (the 640×480 frame cropped to its shape in the centre and scaled);
 * type-3 frames then carry it in their header. Pictures stay 640×480.
 * "ovl <id>" (2.3 HALs that show the preview through an overlay, camera_gb.cpp): the host draws each preview frame
 * into that overlay itself (OverlayFeed), so the picture does not travel through the guest; "ovl 0" stops it.
 * Frames are 640×480 straight from the sensor (the HAL reports the sensor orientation, the guest rotates).
 */
class CameraFeed(private val ctx: Context, private val paths: VmPaths, private val log: (String) -> Unit) {
    private val server = UnixServer(paths.socket("aemu_cam"), "camera") { c -> Session(c).run() }
    @Volatile var onPermissionNeeded: (() -> Unit)? = null

    fun start() = server.start(log)
    fun stop() = server.stop()

    private inner class Session(private val sock: LocalSocket) {
        private val out: OutputStream = sock.outputStream
        private val thread = HandlerThread("guest-camera").apply { start() }
        private val handler = Handler(thread.looper)
        @Volatile private var device: CameraDevice? = null
        @Volatile private var reader: ImageReader? = null
        @Volatile private var streaming = false
        @Volatile private var shot = false
        @Volatile private var onlyNv21 = false
        @Volatile private var overlay = 0
        @Volatile private var zoom = 100
        @Volatile private var outW = W
        @Volatile private var outH = H
        private val nv21 = ByteArray(W * H * 3 / 2)
        private val zbuf = ByteArray(W * H * 3 / 2)
        private var sized = ByteArray(0)
        private val rgba = ByteArray(W * H * 4)
        private var frames = 0
        private var firstNs = 0L
        private var convNs = 0L
        private var sendNs = 0L

        fun run() {
            try {
                val r = sock.inputStream.bufferedReader()
                while (true) {
                    val line = r.readLine() ?: break
                    val p = line.trim().split(' ')
                    when (p[0]) {
                        "open" -> open(p.getOrNull(1) == "1")
                        "start" -> streaming = true
                        "stop" -> streaming = false
                        "shot" -> shot = true
                        "nv21" -> onlyNv21 = true
                        "size" -> {
                            val w = p.getOrNull(1)?.toIntOrNull() ?: W; val h = p.getOrNull(2)?.toIntOrNull() ?: H
                            if (w in 16..1920 && h in 16..1080 && w % 2 == 0 && h % 2 == 0) { outH = h; outW = w }
                        }
                        "zoom" -> zoom = (p.getOrNull(1)?.toIntOrNull() ?: 100).coerceIn(100, 800)
                        "ovl" -> overlay = p.getOrNull(1)?.toIntOrNull() ?: 0
                        "close" -> break
                    }
                }
            } finally {
                streaming = false
                runCatching { device?.close() }; runCatching { reader?.close() }
                thread.quitSafely()
                runCatching { sock.close() }
            }
        }

        @SuppressLint("MissingPermission")
        private fun open(front: Boolean) {
            if (ctx.checkSelfPermission(android.Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) {
                log("camera: the guest wants the camera, asking for permission")
                onPermissionNeeded?.invoke()
                // the guest's open already succeeded: wait for the answer, the preview starts when it is granted
                repeat(120) {
                    if (ctx.checkSelfPermission(android.Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED) return@repeat
                    Thread.sleep(500)
                }
                if (ctx.checkSelfPermission(android.Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) return
            }
            val cm = ctx.getSystemService(CameraManager::class.java)
            val want = if (front) CameraCharacteristics.LENS_FACING_FRONT else CameraCharacteristics.LENS_FACING_BACK
            val id = cm.cameraIdList.firstOrNull { cm.getCameraCharacteristics(it).get(CameraCharacteristics.LENS_FACING) == want }
                ?: cm.cameraIdList.firstOrNull() ?: run { log("camera: the phone has no camera"); return }
            val ir = ImageReader.newInstance(W, H, ImageFormat.YUV_420_888, 2)
            reader = ir
            ir.setOnImageAvailableListener({ r -> r.acquireLatestImage()?.use { frame(it) } }, handler)
            cm.openCamera(id, object : CameraDevice.StateCallback() {
                override fun onOpened(d: CameraDevice) {
                    device = d
                    // the guest may have closed the camera already (mediaserver only probes it at start)
                    runCatching {
                        val req = d.createCaptureRequest(CameraDevice.TEMPLATE_PREVIEW).apply { addTarget(ir.surface) }
                        // a steady 30 frames a second: the default range lets the exposure halve the rate in a dim room
                        cm.getCameraCharacteristics(id).get(CameraCharacteristics.CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES)
                            ?.filter { it.upper == 30 }?.maxByOrNull { it.lower }
                            ?.let { req.set(android.hardware.camera2.CaptureRequest.CONTROL_AE_TARGET_FPS_RANGE, it) }
                        @Suppress("DEPRECATION")
                        d.createCaptureSession(listOf(ir.surface), object : CameraCaptureSession.StateCallback() {
                            override fun onConfigured(s: CameraCaptureSession) { runCatching { s.setRepeatingRequest(req.build(), null, handler) } }
                            override fun onConfigureFailed(s: CameraCaptureSession) { log("camera: session not configured") }
                        }, handler)
                    }.onFailure { runCatching { d.close() }; return }
                    log("camera: ${if (front) "front" else "back"} camera opened for the guest")
                }
                override fun onDisconnected(d: CameraDevice) { d.close() }
                override fun onError(d: CameraDevice, e: Int) { log("camera: error $e"); d.close() }
            }, handler)
        }

        private fun frame(img: Image) {
            if (!streaming && !shot) return
            val t0 = System.nanoTime()
            toNv21(img)
            val z = zoom
            if (z > 100) zoomNv21(z)
            runCatching {
                if (shot) {
                    shot = false
                    val jpg = ByteArrayOutputStream(200_000)
                    YuvImage(nv21, ImageFormat.NV21, W, H, null).compressToJpeg(Rect(0, 0, W, H), 92, jpg)
                    streaming = false   // a picture stops the preview until the guest starts it again (its "start"
                    send(2, jpg.toByteArray(), jpg.size())     // may come right after the picture, so this goes first)
                    return
                }
                val ow = outW; val oh = outH
                val frame = if (ow == W && oh == H) nv21 else resized(ow, oh)
                val ov = overlay
                if (ov != 0) OverlayFeed.show(ov, frame, ow * oh * 3 / 2, ow, oh, OverlayFeed.NV21)
                if (!onlyNv21) toRgba()
                val t1 = System.nanoTime()
                if (onlyNv21) send(3, frame, ow * oh * 3 / 2, w = ow, h = oh) else send(1, rgba, rgba.size, nv21)
                convNs += t1 - t0; sendNs += System.nanoTime() - t1
                if (++frames == 30) {
                    // the host's own picture brightness: a dark guest preview with a dark host frame is the scene, not a bug
                    var sum = 0L; var i = 0; while (i < W * H) { sum += nv21[i].toInt() and 0xff; i += 97 }
                    log("camera: 29 frames in ${(t1 - firstNs) / 1_000_000} ms, conversion ${convNs / 30_000} us, sending ${sendNs / 30_000} us a frame, host luma ${sum * 97 / (W * H)}")
                }
                if (frames == 1) firstNs = t1
            }.onFailure { streaming = false }
        }

        private fun send(type: Int, a: ByteArray, n: Int, b: ByteArray? = null, w: Int = W, h: Int = H) {
            val hd = ByteBuffer.allocate(16).order(ByteOrder.LITTLE_ENDIAN)
            hd.putInt(type).putInt(w).putInt(h).putInt(n + (b?.size ?: 0))
            synchronized(out) { out.write(hd.array()); out.write(a, 0, n); if (b != null) out.write(b); out.flush() }
        }

        /** the frame cropped in the centre to the shape of w×h and scaled to it (nearest pixel) */
        private fun resized(w: Int, h: Int): ByteArray {
            if (sized.size != w * h * 3 / 2) sized = ByteArray(w * h * 3 / 2)
            val cw: Int; val ch: Int
            if (W.toLong() * h > H.toLong() * w) { ch = H; cw = (H.toLong() * w / h).toInt() and 1.inv() } else { cw = W; ch = (W.toLong() * h / w).toInt() and 1.inv() }
            val x0 = ((W - cw) / 2) and 1.inv(); val y0 = ((H - ch) / 2) and 1.inv()
            for (y in 0 until h) {
                val sr = (y0 + y * ch / h) * W + x0
                for (x in 0 until w) sized[y * w + x] = nv21[sr + x * cw / w]
            }
            val n = W * H; val m = w * h
            for (y in 0 until h / 2) {
                val sr = n + ((y0 + y * 2 * ch / h) shr 1) * W
                for (x in 0 until w / 2) {
                    val sc = sr + ((x0 + x * 2 * cw / w) and 1.inv())
                    sized[m + y * w + 2 * x] = nv21[sc]; sized[m + y * w + 2 * x + 1] = nv21[sc + 1]
                }
            }
            return sized
        }

        /** digital zoom: the centre 1/ratio of the frame scaled back to W×H (nearest pixel) */
        private fun zoomNv21(z: Int) {
            val cw = W * 100 / z; val ch = H * 100 / z; val x0 = (W - cw) / 2; val y0 = (H - ch) / 2
            for (y in 0 until H) {
                val sr = (y0 + y * ch / H) * W + x0
                for (x in 0 until W) zbuf[y * W + x] = nv21[sr + x * cw / W]
            }
            val n = W * H
            for (y in 0 until H / 2) {
                val sr = n + ((y0 + y * 2 * ch / H) shr 1) * W
                for (x in 0 until W / 2) {
                    val sc = sr + ((x0 + x * 2 * cw / W) and 1.inv())
                    zbuf[n + y * W + 2 * x] = nv21[sc]; zbuf[n + y * W + 2 * x + 1] = nv21[sc + 1]
                }
            }
            System.arraycopy(zbuf, 0, nv21, 0, zbuf.size)
        }

        /** YUV_420_888 planes (any row/pixel stride) → NV21 */
        private fun toNv21(img: Image) {
            val y = img.planes[0]; val u = img.planes[1]; val v = img.planes[2]
            val yb = y.buffer; val ub = u.buffer; val vb = v.buffer
            for (row in 0 until H) {
                yb.position(row * y.rowStride)
                yb.get(nv21, row * W, minOf(W, yb.remaining()))
            }
            var o = W * H
            if (u.pixelStride == 2 && v.pixelStride == 2) {
                // the usual layout: the V plane's buffer is V,U,V,U… already (one byte short in its last row)
                for (row in 0 until H / 2) {
                    vb.position(row * v.rowStride)
                    val n = minOf(W, vb.remaining())
                    vb.get(nv21, o, n)
                    if (n < W) nv21[o + W - 1] = ub.get(row * u.rowStride + W - 2)
                    o += W
                }
                return
            }
            for (row in 0 until H / 2) {
                val up = row * u.rowStride; val vp = row * v.rowStride
                for (col in 0 until W / 2) {
                    nv21[o++] = vb.get(vp + col * v.pixelStride)
                    nv21[o++] = ub.get(up + col * u.pixelStride)
                }
            }
        }

        private fun toRgba() {
            var o = 0
            for (row in 0 until H) {
                val uv = W * H + (row shr 1) * W
                val yr = row * W
                for (col in 0 until W) {
                    val yy = (nv21[yr + col].toInt() and 0xff) - 16
                    val p = uv + (col and 1.inv())
                    val v = (nv21[p].toInt() and 0xff) - 128
                    val u = (nv21[p + 1].toInt() and 0xff) - 128
                    val c = 1192 * (if (yy < 0) 0 else yy)
                    var r = (c + 1634 * v) shr 10
                    var g = (c - 833 * v - 400 * u) shr 10
                    var b = (c + 2066 * u) shr 10
                    if (r < 0) r = 0 else if (r > 255) r = 255
                    if (g < 0) g = 0 else if (g > 255) g = 255
                    if (b < 0) b = 0 else if (b > 255) b = 255
                    rgba[o++] = r.toByte(); rgba[o++] = g.toByte(); rgba[o++] = b.toByte(); rgba[o++] = -1
                }
            }
        }
    }

    companion object {
        const val W = 640
        const val H = 480
    }
}
