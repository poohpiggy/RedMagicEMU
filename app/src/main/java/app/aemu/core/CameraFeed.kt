package app.aemu.core

import android.annotation.SuppressLint
import android.content.Context
import android.content.pm.PackageManager
import android.graphics.ImageFormat
import android.graphics.Rect
import android.graphics.YuvImage
import android.hardware.camera2.CameraCaptureSession
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CameraConstrainedHighSpeedCaptureSession
import android.hardware.camera2.CameraDevice
import android.hardware.camera2.CameraManager
import android.hardware.camera2.CaptureRequest as CR
import android.hardware.camera2.CaptureResult
import android.hardware.camera2.TotalCaptureResult
import android.media.Image
import android.media.ImageReader
import android.net.LocalSocket
import android.os.Handler
import android.os.HandlerThread
import android.os.SystemClock
import android.util.AtomicFile
import android.util.Size
import android.view.Surface
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.OutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * The phone's cameras for the guest. The emulator's camera HAL (native/camerahal) connects to /dev/socket/aemu_cam
 * and sends "open <0 back | 1 front>", "start", "stop", "shot [left top width height]", "close"; frames go back as
 *     int32 type, width, height, length + data — type 1: RGBA_8888 then NV21 (preview), type 2: JPEG (picture),
 *     type 3: NV21 only (preview, after "nv21").
 * API 36: "video 1" selects the admitted 1080p60 recording session. Type 5 carries
 * an int64 sensor time, an int32 RGBA/YV12 slot, and optional matching control results.
 * Type 6 shares the frame memory. "release <slot>" returns a slot. "video 0" restores photo preview.
 * "controls" updates the NX809J Pro photo session, and "video-controls" applies controls to stock video.
 * Type 7 carries sensor metadata before its image.
 * "zoom <ratio×100>": digital zoom of every frame (preview, recording and picture).
 * "size <w> <h>": NV21 previews of that size (the 640×480 frame cropped to its shape in the centre and scaled);
 * type-3 frames then carry it in their header. Pictures stay 640×480.
 * "upright" (1.x HALs: no display orientation exists): the overlay's picture is turned by the sensor orientation.
 * "ovl <id>" (2.3 HALs that show the preview through an overlay, camera_gb.cpp): the host draws each preview frame
 * into that overlay itself (OverlayFeed), so the picture does not travel through the guest; "ovl 0" stops it.
 * Frames are 640×480 straight from the sensor (the HAL reports the sensor orientation, the guest rotates).
 * Camera service (CameraProfile, 4.0-4.4 HAL): "info" -> type 4 text "<count>;<facing>,<orientation>;..." (one-shot
 * connection, before any open); "params" after open -> type 4 text, the flattened Camera.Parameters of that camera;
 * "set <flattened>" -> what the guest application set, applied to the phone's camera (zoom, flash, focus, exposure,
 * white balance, effect, scene, antibanding; picture size, rotation and JPEG quality for the still). An empty type 4
 * answer (other versions) leaves the HAL on its built-in list.
 */
class CameraFeed(private val ctx: Context, private val paths: VmPaths, private val api: Int, private val log: (String) -> Unit) {
    /** the camera service serves this guest (CameraProfile.API_RANGE): parameters, count and pictures come from it */
    private val served = api in CameraProfile.API_RANGE
    private val server = UnixServer(paths.socket("aemu_cam"), "camera") { c -> Session(c).run() }
    @Volatile var onPermissionNeeded: (() -> Unit)? = null
    private data class StillTarget(
        val id: String, val size: Size, val sensor: Rect?,
        val controls: TargetCameraControls, val raw: TargetCameraRaw?,
    )
    private class RawCapture(val request: Int) {
        var jpeg: ByteArray? = null
        var samples: ByteArray? = null
        var header: ByteArray? = null
    }
    private data class VideoSample(val timestamp: Long, val slot: Int)
    @Volatile private var stillTargets: List<StillTarget?> = emptyList()
    internal var rearVideoMode: TargetCameraVideoMode? = null
        private set

    internal fun prepareRearVideo(): Boolean {
        if (api != 36 || File(paths.bin, "camera.test").isFile) return false
        rearVideoMode = runCatching {
            val manager = ctx.getSystemService(CameraManager::class.java)
            val id = manager.findCamera(false) ?: return@runCatching null
            val camera = manager.getCameraCharacteristics(id)
            if (camera.get(CameraCharacteristics.LENS_FACING) != CameraCharacteristics.LENS_FACING_BACK) null
            else TargetCameraVideoMode.forCamera(camera)
        }.onFailure { log("camera: host video capabilities unavailable: ${it.message}") }.getOrNull()
        return rearVideoMode != null
    }

    internal fun disableRearVideo() {
        rearVideoMode = null
    }

    internal fun prepareStillProfiles(): Boolean {
        publishStillSizes()
        return stillTargets.firstOrNull()?.raw != null
    }

    fun start(): Boolean {
        if (api == 36) {
            if (stillTargets.isEmpty()) prepareStillProfiles()
            File(paths.root, "dev/.aemu-camera-video").writeText(if (rearVideoMode != null) "1" else "0")
        }
        return server.start(log)
    }
    fun stop() = server.stop()

    private fun CameraManager.findCamera(front: Boolean): String? {
        val facing = if (front) CameraCharacteristics.LENS_FACING_FRONT else CameraCharacteristics.LENS_FACING_BACK
        return cameraIdList.firstOrNull { getCameraCharacteristics(it).get(CameraCharacteristics.LENS_FACING) == facing }
            ?: cameraIdList.firstOrNull()
    }

    private fun publishStillSizes() {
        val cm = ctx.getSystemService(CameraManager::class.java)
        val testScene = File(paths.bin, "camera.test").isFile
        val targets = listOf(false, true).map { front ->
            if (testScene) return@map null
            runCatching {
                val id = cm.findCamera(front) ?: return@runCatching null
                val camera = cm.getCameraCharacteristics(id)
                val streams = camera.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP) ?: return@runCatching null
                val jpeg = sequenceOf(
                    streams.getOutputSizes(ImageFormat.JPEG),
                    streams.getHighResolutionOutputSizes(ImageFormat.JPEG),
                ).filterNotNull().flatMap { it.asSequence() }
                    .filter { it.width > 0 && it.height > 0 && it.width.toLong() * it.height <= 64 * 1024 * 1024 }
                    .maxByOrNull { it.width.toLong() * it.height } ?: return@runCatching null
                val raw = if (!front && camera.get(CameraCharacteristics.LENS_FACING) == CameraCharacteristics.LENS_FACING_BACK)
                    runCatching { TargetCameraRaw.forCamera(camera) }
                        .onFailure { log("camera: unable to inspect rear RAW capability: ${it.message}") }
                        .getOrNull()
                else null
                StillTarget(id, jpeg, camera.get(CameraCharacteristics.SENSOR_INFO_ACTIVE_ARRAY_SIZE), TargetCameraControls(camera), raw)
            }.onFailure { log("camera: unable to inspect ${if (front) "front" else "back"} JPEG sizes: ${it.message}") }.getOrNull()
        }
        val sizes = ByteBuffer.allocate(16).order(ByteOrder.LITTLE_ENDIAN)
        targets.forEach { target -> sizes.putInt(target?.size?.width ?: W).putInt(target?.size?.height ?: H) }
        val controls = ByteBuffer.allocate(2 * TargetCameraControls.CAP_BYTES).order(ByteOrder.LITTLE_ENDIAN)
        targets.forEach { target ->
            if (target != null) target.controls.writeCaps(controls)
            else controls.position(controls.position() + TargetCameraControls.CAP_BYTES)
        }
        publishCameraFile("dev/.aemu-camera-jpeg", sizes.array())
        publishCameraFile("dev/.aemu-camera-controls", controls.array())
        publishCameraFile("dev/.aemu-camera-raw", targets.firstOrNull()?.raw?.profile ?: ByteArray(TargetCameraRaw.PROFILE_BYTES))
        targets.firstOrNull()?.raw?.let { log("camera: rear RAW sensor stream ${it.size}") }
        stillTargets = targets
    }

    private fun publishCameraFile(name: String, bytes: ByteArray) {
        val file = AtomicFile(File(paths.root, name))
        val output = file.startWrite()
        try {
            output.write(bytes)
            file.finishWrite(output)
        } catch (failure: Exception) {
            file.failWrite(output)
            throw failure
        }
    }

    private inner class Session(private val sock: LocalSocket) {
        private val out: OutputStream = sock.outputStream
        private val thread = HandlerThread("guest-camera").apply { start() }
        private val handler = Handler(thread.looper)
        @Volatile private var device: CameraDevice? = null
        @Volatile private var reader: ImageReader? = null
        @Volatile private var streaming = false
        @Volatile private var shot = false
        @Volatile private var shotCrop: Rect? = null
        @Volatile private var onlyNv21 = false
        @Volatile private var overlay = 0
        @Volatile private var upright = false
        @Volatile private var sensorOrient = 90
        @Volatile private var zoom = 100
        @Volatile private var outW = W
        @Volatile private var outH = H
        @Volatile private var mirror = false
        @Volatile private var cam: CameraProfile.Cam? = null
        @Volatile private var guestParams: Map<String, String> = emptyMap()
        @Volatile private var still: ImageReader? = null
        private var stillTarget: StillTarget? = null
        private var rawReader: ImageReader? = null
        private var wantRaw = false
        private var rawShot: RawCapture? = null
        private var controls: TargetCameraControls.Request? = null
        private var professional = false
        private var videoControls = false
        private var lastAutoExposure: TotalCaptureResult? = null
        private var lastAutoWhiteBalance: TotalCaptureResult? = null
        private var lastLoggedControls: Any? = null
        private var tint = 0
        private var tintCheckedAt = 0L
        private var tintModified = -1L
        private val previewImages = ResultImages(::frame)
        private val videoResults = LinkedHashMap<Long, TotalCaptureResult>()
        private var pendingVideoSample: VideoSample? = null
        private val jpegImages = ResultImages(::stillDone)
        private val rawImages = ResultImages(::rawDone)
        private val controlResult = ByteBuffer.allocate(48).order(ByteOrder.LITTLE_ENDIAN)
        private val previewCallback = CameraResults()

        private inner class CameraResults(
            private val configuredSession: CameraCaptureSession? = null,
            private val requestTag: Any? = null,
        ) : CameraCaptureSession.CaptureCallback() {
            override fun onCaptureCompleted(s: CameraCaptureSession, r: CR, result: TotalCaptureResult) {
                // High-speed sessions forward capture callbacks from an underlying session object.
                if ((configuredSession ?: s) !== session) return
                if (r.get(CR.CONTROL_AE_MODE) != CR.CONTROL_AE_MODE_OFF) lastAutoExposure = result
                val autoWhiteBalance = r.get(CR.CONTROL_AWB_MODE) != CR.CONTROL_AWB_MODE_OFF
                val previousWbMode = lastAutoWhiteBalance?.get(CaptureResult.CONTROL_AWB_MODE)
                val firstAutoWb = autoWhiteBalance && (lastAutoWhiteBalance == null ||
                    videoControls && previousWbMode != result.get(CaptureResult.CONTROL_AWB_MODE))
                if (autoWhiteBalance) lastAutoWhiteBalance = result
                if (videoSurface != null) {
                    result.get(CaptureResult.SENSOR_TIMESTAMP)?.takeIf { controls != null }?.let { timestamp ->
                        videoResults[timestamp] = result
                        if (videoResults.size > 8) videoResults.remove(videoResults.keys.first())
                    }
                } else if (controls != null) previewImages.result(result)
                val requested = r.tag ?: requestTag
                if ((professional || videoControls) && requested != null && requested != lastLoggedControls) {
                    lastLoggedControls = requested
                    val stream = if (videoControls) "video" else "preview"
                    log("camera: Pro $stream ISO ${result.get(CaptureResult.SENSOR_SENSITIVITY)}, " +
                        "exposure ${result.get(CaptureResult.SENSOR_EXPOSURE_TIME)} ns, " +
                        "EV index ${result.get(CaptureResult.CONTROL_AE_EXPOSURE_COMPENSATION)}, " +
                        "focus ${result.get(CaptureResult.LENS_FOCUS_DISTANCE)}, " +
                        "WB ${result.get(CaptureResult.COLOR_CORRECTION_GAINS)}")
                }
                val tintNeedsReference = firstAutoWb && (if (videoControls) controls?.tint ?: 0 else tint) != 0
                if (professional && refreshTint() || (professional || videoControls) && tintNeedsReference)
                    updatePreviewControls()
            }
        }
        @Volatile private var session: CameraCaptureSession? = null
        @Volatile private var preview: android.hardware.camera2.CaptureRequest.Builder? = null
        @Volatile private var pictureSize = android.util.Size(W, H)
        @Volatile private var closed = false
        private var characteristics: CameraCharacteristics? = null
        private var videoAllowed = false
        private var wantVideo = false
        private var video: TargetCameraVideoFrames? = null
        private var videoSurface: Surface? = null
        private var videoClockOffset = 0L
        private var videoMemory: TargetCameraVideoMemory? = null
        private var sessionGeneration = 0
        private val videoSample = ByteBuffer.allocate(12 + 48).order(ByteOrder.LITTLE_ENDIAN)
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
                        "open" -> {
                            val n = p.getOrNull(1)?.toIntOrNull() ?: 0
                            var front = n == 1
                            if (served) CameraProfile.load(ctx, paths).cams.getOrNull(n)?.let { c ->
                                cam = c; mirror = c.mirror; front = c.facing == 1
                                val q = CameraProfile.effective(ctx, c, api); guestParams = q
                                q["picture-size"]?.let { parseSize(it) }?.let { pictureSize = it }
                            }
                            // off the command loop: "params" must be answered while the phone camera opens (or asks for permission)
                            if (File(paths.bin, "camera.test").exists()) openTest() else handler.post { open(front) }
                        }
                        "start" -> streaming = true
                        "stop" -> streaming = false
                        "shot", "shotraw" -> {
                            handler.post {
                                val raw = p[0] == "shotraw"
                                rawShot = if (raw) {
                                    val request = p.getOrNull(1)?.toUIntOrNull() ?: return@post
                                    RawCapture(request.toInt()).also { capture ->
                                        handler.postDelayed({
                                            if (rawShot === capture) {
                                                rawShot = null
                                                shot = false
                                            }
                                        }, 5_000)
                                    }
                                } else null
                                shotCrop = if (api == 36) parseShotCrop(if (raw) p.drop(1) else p) else null
                                shot = true
                                if (video != null) device?.let { configure(it, useVideo = false) }
                            }
                        }
                        "raw" -> if (api == 36) handler.post {
                            val requested = p.getOrNull(1) == "1"
                            if (wantRaw != requested) {
                                wantRaw = requested
                                if (!requested && rawShot != null) {
                                    rawShot = null
                                    shot = false
                                }
                                if (!wantVideo) device?.let { configure(it, useVideo = false) }
                            }
                        }
                        "video" -> if (api == 36) handler.post {
                            wantVideo = p.getOrNull(1) == "1"
                            if ((video != null) != (videoAllowed && wantVideo) && !shot)
                                device?.let { configure(it, useVideo = wantVideo) }
                        }
                        "controls", "camera-controls", "video-controls" -> if (api == 36) handler.post {
                            val forVideo = p[0] == "video-controls"
                            val parsed = TargetCameraControls.Request.parse(p)
                            val updated = if (forVideo) parsed?.videoOverrides() else parsed
                            val pro = p[0] == "controls" && updated != null
                            if (updated != controls || pro != professional || forVideo != videoControls) {
                                if (pro && !professional) tintCheckedAt = 0
                                professional = pro
                                videoControls = forVideo
                                controls = updated
                                if (pro) refreshTint()
                                updatePreviewControls()
                            }
                        }
                        "release" -> if (api == 36) p.getOrNull(1)?.toIntOrNull()?.let { slot ->
                            handler.post { videoMemory?.release(slot) }
                        }
                        "nv21" -> onlyNv21 = true
                        "upright" -> upright = true
                        "size" -> {
                            val w = p.getOrNull(1)?.toIntOrNull() ?: W; val h = p.getOrNull(2)?.toIntOrNull() ?: H
                            if (w in 16..1920 && h in 16..1080 && w % 2 == 0 && h % 2 == 0) { outH = h; outW = w }
                        }
                        "zoom" -> zoom = (p.getOrNull(1)?.toIntOrNull() ?: 100).coerceIn(100, 800)
                        "ovl" -> overlay = p.getOrNull(1)?.toIntOrNull() ?: 0
                        "info" -> sendText(if (served) info() else "")
                        "params" -> sendText(cam?.let { CameraProfile.flatten(CameraProfile.effective(ctx, it, api).also { q -> guestParams = q }) } ?: "")
                        "set" -> if (served) applyParams(CameraProfile.unflatten(line.trim().substringAfter(' ', "")))
                        "close" -> break
                    }
                }
            } finally {
                closed = true
                streaming = false
                runCatching { sock.close() }
                handler.post {
                    closeOutputs()
                    videoMemory?.close()
                    device?.close()
                    thread.quitSafely()
                }
            }
        }

        @SuppressLint("MissingPermission")
        private fun open(front: Boolean) {
            if (ctx.checkSelfPermission(android.Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) {
                log("camera: the guest wants the camera, asking for permission")
                onPermissionNeeded?.invoke()
                // the guest's open already succeeded: wait for the answer, the preview starts when it is granted
                for (attempt in 0 until 120) {
                    if (closed) return
                    if (ctx.checkSelfPermission(android.Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED) break
                    Thread.sleep(500)
                }
                if (ctx.checkSelfPermission(android.Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) return
            }
            val cm = ctx.getSystemService(CameraManager::class.java)
            stillTarget = stillTargets.getOrNull(if (front) 1 else 0)
            val id = cam?.host?.takeIf { it in cm.cameraIdList }
                ?: stillTarget?.id ?: cm.findCamera(front) ?: run { log("camera: the phone has no camera"); return }
            val info = cm.getCameraCharacteristics(id)
            characteristics = info
            sensorOrient = info.get(CameraCharacteristics.SENSOR_ORIENTATION) ?: 90
            videoAllowed = !front && rearVideoMode != null
            cm.openCamera(id, object : CameraDevice.StateCallback() {
                override fun onOpened(d: CameraDevice) {
                    if (closed) { d.close(); return }
                    device = d
                    configure(d, useVideo = wantVideo)
                    log("camera: ${if (front) "front" else "back"} camera opened for the guest")
                }
                override fun onDisconnected(d: CameraDevice) { d.close() }
                override fun onError(d: CameraDevice, e: Int) { log("camera: error $e"); d.close() }
            }, handler)
        }

        private fun closeOutputs() {
            previewImages.clear()
            pendingVideoSample?.let { videoMemory?.release(it.slot) }
            pendingVideoSample = null
            videoResults.clear()
            lastLoggedControls = null
            jpegImages.clear()
            rawImages.clear()
            session?.close()
            session = null
            preview = null
            reader?.close()
            reader = null
            still?.close()
            still = null
            rawReader?.close()
            rawReader = null
            videoSurface = null
            video?.close()
            video = null
        }

        @Suppress("DEPRECATION")
        private fun configure(d: CameraDevice, useVideo: Boolean) {
            val generation = ++sessionGeneration
            closeOutputs()
            val mode = rearVideoMode?.takeIf { useVideo && videoAllowed }
            runCatching {
                if (mode != null) {
                    // REALTIME sensor timestamps include host suspend time. Guest recorder audio uses monotonic time.
                    videoClockOffset = if (characteristics?.get(CameraCharacteristics.SENSOR_INFO_TIMESTAMP_SOURCE) ==
                        CameraCharacteristics.SENSOR_INFO_TIMESTAMP_SOURCE_REALTIME) {
                        SystemClock.elapsedRealtimeNanos() - System.nanoTime()
                    } else 0L
                    var frameCount = 0
                    var firstTimestamp = 0L
                    val memory = videoMemory ?: TargetCameraVideoMemory().also { newMemory ->
                        try {
                            newMemory.sendDescriptor { fd ->
                                synchronized(out) {
                                    sock.setFileDescriptorsForSend(arrayOf(fd))
                                    try {
                                        send(6, ByteArray(0), 0, w = TargetCameraVideoMode.WIDTH, h = TargetCameraVideoMode.HEIGHT)
                                    } finally {
                                        sock.setFileDescriptorsForSend(null)
                                    }
                                }
                            }
                        } catch (error: Exception) {
                            newMemory.close()
                            throw error
                        }
                        videoMemory = newMemory
                    }
                    val source = TargetCameraVideoFrames(
                        memory = memory,
                        shouldRead = { streaming && !closed },
                        onFrame = { timestamp, slot ->
                            if (streaming && !closed) {
                                queueVideoSample(timestamp, slot)
                                if (frameCount++ == 0) firstTimestamp = timestamp
                                if (frameCount == 121) {
                                    val rate = 120_000_000_000L / (timestamp - firstTimestamp)
                                    log("camera: 1080p source delivered $frameCount frames at $rate fps")
                                }
                            } else memory.release(slot)
                        },
                        onFailure = { failure ->
                            if (!closed) log("camera: host video frame failed: ${failure.message}")
                            streaming = false
                            closeOutputs()
                        },
                    )
                    video = source
                    val surface = source.open(handler)
                    videoSurface = surface
                    val callback = captureSession(generation) { s ->
                        repeatVideo(s, d, surface, mode)
                    }
                    if (mode.constrained) d.createConstrainedHighSpeedCaptureSession(listOf(surface), callback, handler)
                    else d.createCaptureSession(listOf(surface), callback, handler)
                } else {
                    val ir = ImageReader.newInstance(W, H, ImageFormat.YUV_420_888, 2)
                    reader = ir
                    ir.setOnImageAvailableListener({ r ->
                        if (r === reader) r.acquireLatestImage()?.let { image ->
                            if (controls == null) image.use { frame(it, null) }
                            else previewImages.image(image)
                        }
                    }, handler)
                    val request = previewRequest(d, ir)
                    preview = request
                    val size = if (served) pictureSize else stillTarget?.size
                    val st = size?.let {
                        runCatching { ImageReader.newInstance(it.width, it.height, ImageFormat.JPEG, 2) }
                            .onFailure { error -> log("camera: cannot allocate JPEG reader for $it: ${error.message}") }
                            .getOrNull()
                    }
                    still = st
                    st?.setOnImageAvailableListener({ r ->
                        if (r === still) r.acquireLatestImage()?.let { image ->
                            if (controls == null && rawShot == null) image.use { stillDone(it, null) }
                            else jpegImages.image(image)
                        }
                    }, handler)
                    val raw = stillTarget?.raw?.takeIf { wantRaw }?.let {
                        ImageReader.newInstance(it.size.width, it.size.height, ImageFormat.RAW_SENSOR, 2)
                    }
                    rawReader = raw
                    raw?.setOnImageAvailableListener({ r ->
                        if (r === rawReader) r.acquireNextImage()?.let(rawImages::image)
                    }, handler)
                    val callback = captureSession(generation) { s ->
                        s.setRepeatingRequest((preview ?: request).build(), if (api == 36) previewCallback else null, handler)
                    }
                    d.createCaptureSession(listOfNotNull(ir.surface, st?.surface, raw?.surface), callback, handler)
                }
            }.onFailure { log("camera: unable to configure host camera: ${it.message}"); closeOutputs() }
        }

        private fun queueVideoSample(timestamp: Long, slot: Int) {
            // The host may deliver result metadata after the texture callback. Hold one sample at most.
            pendingVideoSample?.let { sendVideoSample(it.timestamp, it.slot, videoResults.remove(it.timestamp)) }
            pendingVideoSample = null
            val result = videoResults.remove(timestamp)
            if (result == null && controls != null) pendingVideoSample = VideoSample(timestamp, slot)
            else sendVideoSample(timestamp, slot, result)
        }

        private fun sendVideoSample(timestamp: Long, slot: Int, result: TotalCaptureResult?) {
            videoSample.clear()
            videoSample.putLong(timestamp - videoClockOffset).putInt(slot)
            encodeControlResult(result)?.let { videoSample.put(it) }
            send(5, videoSample.array(), videoSample.position(),
                w = TargetCameraVideoMode.WIDTH, h = TargetCameraVideoMode.HEIGHT)
        }

        private fun previewRequest(device: CameraDevice, reader: ImageReader): CR.Builder =
            device.createCaptureRequest(CameraDevice.TEMPLATE_PREVIEW).apply {
                addTarget(reader.surface)
                characteristics?.get(CameraCharacteristics.CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES)
                    ?.filter { it.upper == 30 }?.maxByOrNull { it.lower }
                    ?.let { set(CR.CONTROL_AE_TARGET_FPS_RANGE, it) }
                if (served) applyTo(this)
                setProControls(this)
            }

        private fun setProControls(request: CR.Builder, videoFramePeriod: Long? = if (videoControls) 33_333_333L else null) {
            controls?.let {
                val appliedTint = if (videoControls) it.tint else if (professional) tint else 0
                stillTarget?.controls?.apply(request, it, lastAutoExposure, lastAutoWhiteBalance, appliedTint, videoFramePeriod)
                request.setTag(it to appliedTint)
            }
        }

        private fun repeatVideo(s: CameraCaptureSession, d: CameraDevice, surface: Surface, mode: TargetCameraVideoMode) {
            val builder = d.createCaptureRequest(CameraDevice.TEMPLATE_RECORD).apply {
                addTarget(surface)
                set(CR.CONTROL_AE_TARGET_FPS_RANGE, mode.fps)
            }
            fun submit(request: CR) {
                val results = CameraResults(s, request.tag)
                if (mode.constrained) {
                    val high = s as CameraConstrainedHighSpeedCaptureSession
                    high.setRepeatingBurst(high.createHighSpeedRequestList(request), results, handler)
                } else s.setRepeatingRequest(request, results, handler)
            }
            val automatic = if (controls != null) builder.build() else null
            try {
                setProControls(builder, 1_000_000_000L / mode.fps.upper)
                submit(builder.build())
            } catch (error: IllegalArgumentException) {
                if (automatic == null) throw error
                log("camera: host rejected manual video settings, restoring automatic controls: ${error.message}")
                submit(automatic)
            }
        }

        private fun updatePreviewControls() {
            val d = device ?: return
            runCatching {
                val surface = videoSurface
                val mode = rearVideoMode
                if (surface != null && mode != null) {
                    session?.let { repeatVideo(it, d, surface, mode) }
                } else {
                    val ir = reader ?: return
                    val request = previewRequest(d, ir)
                    preview = request
                    session?.setRepeatingRequest(request.build(), previewCallback, handler)
                }
            }.onFailure { log("camera: manual controls failed: ${it.message}") }
        }

        private fun refreshTint(): Boolean {
            val now = SystemClock.uptimeMillis()
            if (now - tintCheckedAt < 150) return false
            tintCheckedAt = now
            // NX809J persists the Pro tint control without setting a Camera2 gain.
            val prefs = File(paths.root, "data/data/com.android.camera/shared_prefs/com.android.camera_PRO_0.xml")
            val modified = prefs.lastModified()
            if (modified == tintModified) return false
            tintModified = modified
            val previous = tint
            tint = runCatching {
                if (modified == 0L || prefs.length() > 128 * 1024 ||
                    !prefs.canonicalFile.toPath().startsWith(paths.root.canonicalFile.toPath())) 0
                else PRO_TINT.find(prefs.readText())?.groupValues?.get(1)?.toIntOrNull() ?: 0
            }.onFailure { log("camera: unable to read Pro tint: ${it.message}") }.getOrDefault(0)
            return tint != previous
        }

        /** The ImageReader and Camera2 result callback can arrive in either order. */
        private inner class ResultImages(private val deliver: (Image, TotalCaptureResult?) -> Unit) {
            private val results = LinkedHashMap<Long, TotalCaptureResult>()
            private var pending: Image? = null

            fun image(image: Image) {
                finish(null)
                val result = results.remove(image.timestamp)
                if (result != null) image.use { deliver(it, result) }
                else {
                    pending = image
                    handler.postDelayed({ if (pending === image) finish(null) }, if (rawShot != null) 750 else 100)
                }
            }

            fun result(result: TotalCaptureResult) {
                val timestamp = result.get(CaptureResult.SENSOR_TIMESTAMP) ?: return
                if (pending?.timestamp == timestamp) finish(result)
                else {
                    results[timestamp] = result
                    if (results.size > 4) results.remove(results.keys.first())
                }
            }

            fun clear() {
                pending?.close()
                pending = null
                results.clear()
            }

            private fun finish(result: TotalCaptureResult?) {
                val image = pending ?: return
                pending = null
                image.use { deliver(it, result) }
            }
        }

        private fun captureSession(generation: Int, start: (CameraCaptureSession) -> Unit) =
            object : CameraCaptureSession.StateCallback() {
                override fun onConfigured(s: CameraCaptureSession) {
                    if (closed || generation != sessionGeneration) { s.close(); return }
                    session = s
                    runCatching { start(s) }.onFailure { log("camera: unable to start host capture: ${it.message}") }
                }

                override fun onConfigureFailed(s: CameraCaptureSession) {
                    s.close()
                    if (closed || generation != sessionGeneration) return
                    log("camera: host camera rejected capture session")
                    if (rawReader != null) {
                        wantRaw = false
                        rawShot = null
                        shot = false
                        log("camera: restoring JPEG preview after host rejected RAW combination")
                        device?.let { configure(it, useVideo = false) }
                    }
                }
            }

        private fun frame(img: Image, result: TotalCaptureResult?) {
            if (!streaming && !shot) return
            val t0 = System.nanoTime()
            toNv21(img)
            if (mirror) mirrorNv21()
            process(t0, result)
        }

        private fun sendControlResult(result: TotalCaptureResult?) {
            val data = encodeControlResult(result) ?: return
            send(7, data, data.size, w = 0, h = 0)
        }

        private fun encodeControlResult(result: TotalCaptureResult?): ByteArray? {
            val exposure = result?.get(CaptureResult.SENSOR_EXPOSURE_TIME) ?: return null
            val sensitivity = result.get(CaptureResult.SENSOR_SENSITIVITY) ?: return null
            controlResult.clear()
            controlResult.putLong(exposure)
            controlResult.putLong(result.get(CaptureResult.SENSOR_FRAME_DURATION) ?: exposure)
            controlResult.putInt(sensitivity)
            controlResult.putInt(result.get(CaptureResult.CONTROL_AE_EXPOSURE_COMPENSATION) ?: 0)
            controlResult.putFloat(result.get(CaptureResult.LENS_FOCUS_DISTANCE) ?: 0f)
            controlResult.put((result.get(CaptureResult.CONTROL_AE_STATE) ?: 0).toByte())
            controlResult.put((result.get(CaptureResult.CONTROL_AF_STATE) ?: 0).toByte())
            controlResult.put((result.get(CaptureResult.CONTROL_AWB_STATE) ?: 0).toByte())
            controlResult.put((result.get(CaptureResult.LENS_STATE) ?: 0).toByte())
            val gains = result.get(CaptureResult.COLOR_CORRECTION_GAINS)
            controlResult.putFloat(gains?.red ?: 0f).putFloat(gains?.greenEven ?: 0f)
            controlResult.putFloat(gains?.greenOdd ?: 0f).putFloat(gains?.blue ?: 0f)
            return controlResult.array()
        }

        /** the test picture instead of the camera (a file "camera.test" in the image's run directory): see TestScene */
        private fun openTest() {
            val cm = ctx.getSystemService(CameraManager::class.java)
            val orient = runCatching { cm.cameraIdList.firstNotNullOfOrNull { id -> cm.getCameraCharacteristics(id)
                .takeIf { it.get(CameraCharacteristics.LENS_FACING) == CameraCharacteristics.LENS_FACING_BACK }
                ?.get(CameraCharacteristics.SENSOR_ORIENTATION) } }.getOrNull() ?: 90
            sensorOrient = orient
            val scene = TestScene(W, H, orient)
            log("camera: test picture for the guest (sensor orientation $orient)")
            val start = android.os.SystemClock.uptimeMillis()
            val tick = object : Runnable {
                var n = 0
                override fun run() {
                    if (!thread.isAlive) return
                    if (streaming || shot) { val t0 = System.nanoTime(); scene.draw(n++, nv21); runCatching { process(t0) } }
                    handler.postAtTime(this, start + (n + 1) * 1000L / 30)
                }
            }
            handler.post(tick)
        }

        private fun process(t0: Long, result: TotalCaptureResult? = null) {
            val z = zoom
            if (z > 100) zoomNv21(z)
            runCatching {
                if (shot && still != null && session != null && device != null) {
                    shot = false
                    if (takeStill()) return
                    shot = true
                }
                if (shot && rawShot != null) return
                if (shot) {
                    val crop = shotCrop
                    shot = false
                    if (crop != null) cropNv21(crop.left, crop.top, crop.width(), crop.height())
                    val jpg = ByteArrayOutputStream(200_000)
                    YuvImage(nv21, ImageFormat.NV21, W, H, null).compressToJpeg(Rect(0, 0, W, H), 92, jpg)
                    streaming = false   // a picture stops the preview until the guest starts it again (its "start"
                    send(2, jpg.toByteArray(), jpg.size())     // may come right after the picture, so this goes first)
                    return
                }
                val ow = outW; val oh = outH
                val frame = if (ow == W && oh == H) nv21 else resized(ow, oh)
                val ov = overlay
                // "upright": the picture turned clockwise by the sensor orientation (HAL_TRANSFORM_ROT_90 4, ROT_180 3,
                // ROT_270 7), as a 1.x phone's own HAL shows its preview: no display orientation reaches it
                val turn = if (!upright) 0 else when (sensorOrient) { 90 -> 4; 180 -> 3; 270 -> 7; else -> 0 }
                if (ov != 0) OverlayFeed.show(ov, frame, ow * oh * 3 / 2, ow, oh, OverlayFeed.NV21, unplaced = true, turn = turn)
                if (!onlyNv21) toRgba()
                val t1 = System.nanoTime()
                if (onlyNv21) send(3, frame, ow * oh * 3 / 2, w = ow, h = oh)
                else {
                    if (controls != null) sendControlResult(result)
                    send(1, rgba, rgba.size, nv21)
                }
                convNs += t1 - t0; sendNs += System.nanoTime() - t1
                if (++frames == 30) {
                    // the host's own picture brightness: a dark guest preview with a dark host frame is the scene, not a bug
                    var sum = 0L; var i = 0; while (i < W * H) { sum += nv21[i].toInt() and 0xff; i += 97 }
                    log("camera: 29 frames in ${(t1 - firstNs) / 1_000_000} ms, conversion ${convNs / 30_000} us, sending ${sendNs / 30_000} us a frame, host luma ${sum * 97 / (W * H)}")
                }
                if (frames == 1) firstNs = t1
            }.onFailure { streaming = false }
        }

        private fun sendText(t: String) { val b = t.toByteArray(); send(4, b, b.size, w = 0, h = 0) }

        /** "<count>;<facing>,<orientation>;..." from the profile */
        private fun info(): String {
            val cams = CameraProfile.load(ctx, paths).cams
            return cams.size.toString() + cams.joinToString("") { ";${it.facing},${it.orientation}" }
        }

        private fun parseSize(s: String): android.util.Size? {
            val i = s.indexOf('x'); if (i <= 0) return null
            val w = s.substring(0, i).toIntOrNull() ?: return null; val h = s.substring(i + 1).toIntOrNull() ?: return null
            return if (w in 16..8192 && h in 16..8192) android.util.Size(w, h) else null
        }

        /** what the guest application set (Camera.setParameters): kept, and the phone's camera follows it */
        private fun applyParams(p: Map<String, String>) {
            guestParams = p
            val ratios = p["zoom-ratios"]?.split(',')?.mapNotNull { it.toIntOrNull() }
            p["zoom"]?.toIntOrNull()?.let { i -> ratios?.getOrNull(i)?.let { zoom = it.coerceIn(100, 800) } }
            p["preview-size"]?.let { parseSize(it) }?.let { if (it.width % 2 == 0 && it.height % 2 == 0 && it.width <= 1920 && it.height <= 1080) { outW = it.width; outH = it.height } }
            val ps = p["picture-size"]?.let { parseSize(it) }
            if (ps != null && ps != pictureSize) { pictureSize = ps; handler.post { reconfigure() } }
            preview?.let { r -> handler.post { runCatching { applyTo(r); session?.setRepeatingRequest(r.build(), null, handler) } } }
        }

        /** the guest's parameters as camera2 controls of the phone's camera */
        private fun applyTo(r: CR.Builder) {
            val p = guestParams
            fun mode(v: String?, m: Map<String, Int>) = v?.let { m[it] }
            mode(p["focus-mode"], mapOf("auto" to 1, "macro" to 2, "continuous-video" to 3, "continuous-picture" to 4, "edof" to 5, "fixed" to 0, "infinity" to 0))
                ?.let { r.set(CR.CONTROL_AF_MODE, it); if (p["focus-mode"] == "infinity") r.set(CR.LENS_FOCUS_DISTANCE, 0f) }
            mode(p["whitebalance"], mapOf("auto" to 1, "incandescent" to 2, "fluorescent" to 3, "warm-fluorescent" to 4, "daylight" to 5, "cloudy-daylight" to 6, "twilight" to 7, "shade" to 8))
                ?.let { r.set(CR.CONTROL_AWB_MODE, it) }
            mode(p["effect"], mapOf("none" to 0, "mono" to 1, "negative" to 2, "solarize" to 3, "sepia" to 4, "posterize" to 5, "whiteboard" to 6, "blackboard" to 7, "aqua" to 8))
                ?.let { r.set(CR.CONTROL_EFFECT_MODE, it) }
            mode(p["antibanding"], mapOf("off" to 0, "50hz" to 1, "60hz" to 2, "auto" to 3))
                ?.let { r.set(CR.CONTROL_AE_ANTIBANDING_MODE, it) }
            val scene = mode(p["scene-mode"], mapOf("action" to 2, "portrait" to 3, "landscape" to 4, "night" to 5, "night-portrait" to 6, "theatre" to 7, "beach" to 8, "snow" to 9, "sunset" to 10, "steadyphoto" to 11, "fireworks" to 12, "sports" to 13, "party" to 14, "candlelight" to 15, "barcode" to 16, "hdr" to 18))
            r.set(CR.CONTROL_MODE, if (scene != null) 2 else 1)
            if (scene != null) r.set(CR.CONTROL_SCENE_MODE, scene)
            p["exposure-compensation"]?.toIntOrNull()?.let { r.set(CR.CONTROL_AE_EXPOSURE_COMPENSATION, it) }
            p["auto-exposure-lock"]?.let { r.set(CR.CONTROL_AE_LOCK, it == "true") }
            p["auto-whitebalance-lock"]?.let { r.set(CR.CONTROL_AWB_LOCK, it == "true") }
            when (p["flash-mode"]) {
                "torch" -> { r.set(CR.CONTROL_AE_MODE, 1); r.set(CR.FLASH_MODE, 2) }
                "on" -> { r.set(CR.CONTROL_AE_MODE, 3); r.set(CR.FLASH_MODE, 0) }
                "auto" -> { r.set(CR.CONTROL_AE_MODE, 2); r.set(CR.FLASH_MODE, 0) }
                "off" -> { r.set(CR.CONTROL_AE_MODE, 1); r.set(CR.FLASH_MODE, 0) }
            }
        }

        /** a new picture size: the still stream is rebuilt (camera2 fixes stream sizes per session) */
        private fun reconfigure() {
            val d = device ?: return; val ir = reader ?: return; val r = preview ?: return
            runCatching { session?.close() }; runCatching { still?.close() }
            val st = runCatching { ImageReader.newInstance(pictureSize.width, pictureSize.height, ImageFormat.JPEG, 2) }.getOrNull()
            still = st; session = null
            st?.setOnImageAvailableListener({ rr -> rr.acquireLatestImage()?.use { stillDone(it, null) } }, handler)
            @Suppress("DEPRECATION")
            runCatching {
                d.createCaptureSession(listOfNotNull(ir.surface, st?.surface), object : CameraCaptureSession.StateCallback() {
                    override fun onConfigured(s: CameraCaptureSession) { session = s; runCatching { s.setRepeatingRequest(r.build(), null, handler) } }
                    override fun onConfigureFailed(s: CameraCaptureSession) { log("camera: picture size $pictureSize not configured") }
                }, handler)
            }
        }

        /** the picture through the phone's JPEG stream; false: none (the preview frame is used instead) */
        private fun takeStill(): Boolean {
            val d = device ?: return false; val s = session ?: return false; val st = still ?: return false
            val rawCapture = rawShot
            val raw = if (rawCapture != null) rawReader ?: return false else null
            return runCatching {
                val r = d.createCaptureRequest(CameraDevice.TEMPLATE_STILL_CAPTURE).apply { addTarget(st.surface) }
                if (raw != null) r.addTarget(raw.surface)
                if (served) applyTo(r)
                setProControls(r)
                if (rawCapture != null) r.setTag(rawCapture)
                val p = guestParams
                r.set(CR.JPEG_QUALITY, (p["jpeg-quality"]?.toIntOrNull() ?: 90).coerceIn(1, 100).toByte())
                // the guest's "rotation" (Parameters.setRotation): the turn its camera application wants in the file
                r.set(CR.JPEG_ORIENTATION, (p["rotation"]?.toIntOrNull() ?: 0).let { (it % 360 + 360) % 360 })
                val tw = p["jpeg-thumbnail-width"]?.toIntOrNull() ?: 0; val th = p["jpeg-thumbnail-height"]?.toIntOrNull() ?: 0
                r.set(CR.JPEG_THUMBNAIL_SIZE, Size(tw, th))
                r.set(CR.JPEG_THUMBNAIL_QUALITY, (p["jpeg-thumbnail-quality"]?.toIntOrNull() ?: 90).coerceIn(1, 100).toByte())
                val sensor = stillTarget?.sensor
                val crop = shotCrop
                if (sensor != null && crop != null) {
                    r.set(CR.SCALER_CROP_REGION, Rect(
                        sensor.left + crop.left * sensor.width() / W,
                        sensor.top + crop.top * sensor.height() / H,
                        sensor.left + crop.right * sensor.width() / W,
                        sensor.top + crop.bottom * sensor.height() / H,
                    ))
                }
                jpegImages.clear()
                if (raw != null) rawImages.clear()
                val callback = if (controls != null || raw != null) object : CameraCaptureSession.CaptureCallback() {
                    override fun onCaptureCompleted(s: CameraCaptureSession, request: CR, result: TotalCaptureResult) {
                        if (s !== session) return
                        if (professional) log("camera: Pro JPEG ISO ${result.get(CaptureResult.SENSOR_SENSITIVITY)}, " +
                            "exposure ${result.get(CaptureResult.SENSOR_EXPOSURE_TIME)} ns, " +
                            "focus ${result.get(CaptureResult.LENS_FOCUS_DISTANCE)}")
                        jpegImages.result(result)
                        if (raw != null) rawImages.result(result)
                    }
                } else null
                s.capture(r.build(), callback, handler)
                true
            }.getOrDefault(false)
        }

        private fun stillDone(img: Image, result: TotalCaptureResult?) {
            val rawCapture = result?.request?.tag as? RawCapture
            if (rawShot != null || rawCapture != null) {
                if (rawCapture == null || rawCapture !== rawShot) return
                val buffer = img.planes.single().buffer
                rawCapture.jpeg = ByteArray(buffer.remaining()).also { buffer.get(it) }
                sendRawCapture(rawCapture)
                return
            }
            val b = img.planes[0].buffer
            val a = ByteArray(b.remaining()); b.get(a)
            streaming = false   // a picture stops the preview until the guest starts it again
            if (api == 36) {
                sendControlResult(result)
                send(2, a, a.size, w = img.width, h = img.height)
            } else send(2, a, a.size)
            log("camera: picture ${img.width}x${img.height}, ${a.size / 1024} KB")
            if (wantVideo) device?.let { configure(it, useVideo = true) }
        }

        private fun rawDone(image: Image, result: TotalCaptureResult?) {
            val target = stillTarget?.raw ?: return
            val capture = result?.request?.tag as? RawCapture ?: return
            if (capture !== rawShot) return
            runCatching {
                val metadata = TargetCameraRaw.result(result)
                val measured = encodeControlResult(result)
                check(metadata != null && measured != null) { "RAW sensor result lacks DNG metadata" }
                capture.samples = target.samples(image)
                capture.header = ByteBuffer.allocate(4 + metadata.size + measured.size)
                    .order(ByteOrder.LITTLE_ENDIAN).putInt(capture.request).put(metadata).put(measured).array()
                sendRawCapture(capture)
            }.onFailure { log("camera: host RAW capture failed: ${it.message}") }
        }

        private fun sendRawCapture(capture: RawCapture) {
            val target = stillTarget?.raw ?: return
            val header = capture.header ?: return
            val samples = capture.samples ?: return
            val jpeg = capture.jpeg ?: return
            rawShot = null
            runCatching {
                send(8, header, header.size, samples, target.size.width, target.size.height, jpeg)
                log("camera: RAW ${target.size} paired with ${jpeg.size / 1024} KB JPEG")
            }.onFailure { log("camera: unable to send RAW capture: ${it.message}") }
        }

        /** the frame flipped left-right (a "mirror" camera of the profile) */
        private fun mirrorNv21() {
            for (y in 0 until H) { var a = y * W; var b = a + W - 1; while (a < b) { val t = nv21[a]; nv21[a] = nv21[b]; nv21[b] = t; a++; b-- } }
            val n = W * H
            for (y in 0 until H / 2) {
                var a = n + y * W; var b = a + W - 2
                while (a < b) { val v = nv21[a]; val u = nv21[a + 1]; nv21[a] = nv21[b]; nv21[a + 1] = nv21[b + 1]; nv21[b] = v; nv21[b + 1] = u; a += 2; b -= 2 }
            }
        }

        private fun send(
            type: Int, a: ByteArray, n: Int, b: ByteArray? = null, w: Int = W, h: Int = H, jpeg: ByteArray? = null,
        ) {
            val hd = ByteBuffer.allocate(16).order(ByteOrder.LITTLE_ENDIAN)
            hd.putInt(type).putInt(w).putInt(h).putInt(n + (b?.size ?: 0) + (jpeg?.size ?: 0))
            synchronized(out) {
                out.write(hd.array())
                if (n > 0) out.write(a, 0, n)
                if (b != null) out.write(b)
                if (jpeg != null) out.write(jpeg)
                out.flush()
            }
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

        private fun parseShotCrop(command: List<String>): Rect? {
            if (command.size != 5) return null
            val left = command[1].toIntOrNull() ?: return null
            val top = command[2].toIntOrNull() ?: return null
            val width = command[3].toIntOrNull() ?: return null
            val height = command[4].toIntOrNull() ?: return null
            if (left !in 0 until W || top !in 0 until H || width !in 1..(W - left) || height !in 1..(H - top)) return null
            return Rect(left, top, left + width, top + height)
        }

        /** digital zoom: the centre 1/ratio of the frame scaled back to W×H (nearest pixel) */
        private fun zoomNv21(z: Int) {
            val cw = W * 100 / z
            val ch = H * 100 / z
            cropNv21((W - cw) / 2, (H - ch) / 2, cw, ch)
        }

        private fun cropNv21(x0: Int, y0: Int, cw: Int, ch: Int) {
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
        private val PRO_TINT = Regex("""<int\s+name="pref_camera_color_tint_key"\s+value="(-?\d+)"\s*/>""")
        const val W = 640
        const val H = 480
    }
}

/**
 * A known picture for checking a guest's camera path: the scene as a person holding the phone upright would see it
 * (an arrow pointing up with "UP", the corners marked TL red, TR green, BL blue, BR white, the colour bars
 * R G B C M Y W K left to right, a bar running left to right once a second and the frame number), turned into the
 * sensor's own frame by the host back camera's SENSOR_ORIENTATION (CameraCharacteristics: the clockwise turn that
 * makes the sensor image upright on the screen in its natural orientation), as the real sensor delivers it.
 * A guest that shows its preview right shows the arrow up and TL in the top left corner; a mirrored or turned
 * picture shows at once which way it is wrong, and the running bar and the counter show dropped or uneven frames.
 */
class TestScene(private val w: Int, private val h: Int, private val orient: Int) {
    private val sw = if (orient % 180 == 0) w else h   // the upright scene
    private val sh = if (orient % 180 == 0) h else w
    private val bmp = android.graphics.Bitmap.createBitmap(sw, sh, android.graphics.Bitmap.Config.ARGB_8888)
    private val cv = android.graphics.Canvas(bmp)
    private val pt = android.graphics.Paint(android.graphics.Paint.ANTI_ALIAS_FLAG)
    private val px = IntArray(sw * sh)

    fun draw(n: Int, out: ByteArray) {
        cv.drawColor(0xff404040.toInt())
        val bars = intArrayOf(0xffff0000.toInt(), 0xff00ff00.toInt(), 0xff0000ff.toInt(), 0xff00ffff.toInt(),
            0xffff00ff.toInt(), 0xffffff00.toInt(), 0xffffffff.toInt(), 0xff000000.toInt())
        val by = sh * 2 / 3f; val bh = sh / 6f
        for (i in bars.indices) { pt.color = bars[i]; cv.drawRect(i * sw / 8f, by, (i + 1) * sw / 8f, by + bh, pt) }
        val c = sw / 8f
        fun corner(x: Float, y: Float, col: Int, t: String) {
            pt.color = col; cv.drawRect(x, y, x + c, y + c, pt)
            pt.color = if (col == 0xffffffff.toInt()) 0xff000000.toInt() else 0xffffffff.toInt(); pt.textSize = c * 0.45f
            cv.drawText(t, x + c * 0.12f, y + c * 0.65f, pt)
        }
        corner(0f, 0f, 0xffff0000.toInt(), "TL"); corner(sw - c, 0f, 0xff00ff00.toInt(), "TR")
        corner(0f, sh - c, 0xff0000ff.toInt(), "BL"); corner(sw - c, sh - c, 0xffffffff.toInt(), "BR")
        // the arrow up
        pt.color = 0xffffffff.toInt()
        val ax = sw / 2f; val top = sh * 0.08f; val aw = sw * 0.18f
        val path = android.graphics.Path().apply { moveTo(ax, top); lineTo(ax + aw, top + aw); lineTo(ax + aw / 3, top + aw)
            lineTo(ax + aw / 3, sh * 0.5f); lineTo(ax - aw / 3, sh * 0.5f); lineTo(ax - aw / 3, top + aw); lineTo(ax - aw, top + aw); close() }
        cv.drawPath(path, pt)
        pt.textSize = sh / 14f; pt.color = 0xffffff00.toInt()
        cv.drawText("UP", ax - pt.measureText("UP") / 2, sh * 0.58f, pt)
        pt.textSize = sh / 18f; pt.color = 0xffffffff.toInt()
        cv.drawText("#$n", c * 1.2f, c * 0.7f, pt)
        // one sweep a second at 30 frames
        val x = (n % 30) * sw / 30f
        pt.color = 0xffff8000.toInt(); cv.drawRect(x, by - bh / 2, x + sw / 30f, by, pt)
        bmp.getPixels(px, 0, sw, 0, 0, sw, sh)
        // into the sensor's frame: the sensor image turned clockwise by orient is the scene, so the sensor pixel
        // (sx, sy) is the scene pixel turned back
        val n2 = w * h
        for (sy in 0 until h) for (sx in 0 until w) {
            val (x0, y0) = when (orient) { 90 -> Pair(h - 1 - sy, sx); 180 -> Pair(w - 1 - sx, h - 1 - sy); 270 -> Pair(sy, w - 1 - sx); else -> Pair(sx, sy) }
            val p = px[y0 * sw + x0]
            val r = (p shr 16) and 255; val g = (p shr 8) and 255; val b = p and 255
            out[sy * w + sx] = ((66 * r + 129 * g + 25 * b + 128 shr 8) + 16).toByte()
            if (sx and 1 == 0 && sy and 1 == 0) {
                val o = n2 + (sy / 2) * w + sx
                out[o] = ((112 * r - 94 * g - 18 * b + 128 shr 8) + 128).toByte()       // V
                out[o + 1] = ((-38 * r - 74 * g + 112 * b + 128 shr 8) + 128).toByte()  // U
            }
        }
    }
}
