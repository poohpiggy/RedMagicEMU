package app.aemu.core

import android.graphics.SurfaceTexture
import android.hardware.camera2.CameraCharacteristics
import android.media.MediaRecorder
import android.opengl.EGL14
import android.opengl.EGLContext
import android.opengl.EGLDisplay
import android.opengl.EGLSurface
import android.opengl.GLES11Ext
import android.opengl.GLES20
import android.os.Handler
import android.os.Parcel
import android.os.SharedMemory
import android.util.Range
import android.util.Size
import android.view.Surface
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.io.FileDescriptor
import kotlin.math.abs

/** Keep the camera buffer in sensor coordinates so the Guest can orient it for display. */
internal fun alignCameraVideoTexture(transform: FloatArray): Boolean {
    if (abs(transform[1]) <= abs(transform[0]) || abs(transform[4]) <= abs(transform[5])) return false

    // Compose the opposite quarter turn of the unit square into the input coordinates.
    // This leaves the SurfaceTexture crop and texture-space flip in the sampling matrix.
    val negativeX = transform[4] < 0f
    for (row in 0 until 4) {
        val x = transform[row]
        val y = transform[row + 4]
        transform[row] = if (negativeX) -y else y
        transform[row + 4] = if (negativeX) x else -x
        transform[row + 12] += if (negativeX) y else x
    }
    return true
}

internal class TargetCameraVideoMemory : AutoCloseable {
    private val pixels = TargetCameraVideoMode.WIDTH * TargetCameraVideoMode.HEIGHT
    private val rgbaBytes = pixels * 4
    private val frameBytes = rgbaBytes + pixels * 3 / 2
    private val memory = SharedMemory.create("guest-camera-video", frameBytes * 4)
    private val bytes = try {
        memory.mapReadWrite()
    } catch (failure: Exception) {
        memory.close()
        throw failure
    }
    private val busy = BooleanArray(4)
    private val rgbaSlots = Array(4) { slice(it * frameBytes, rgbaBytes) }
    private val yv12Slots = Array(4) { slice(it * frameBytes + rgbaBytes, frameBytes - rgbaBytes) }

    fun sendDescriptor(send: (FileDescriptor) -> Unit) {
        val parcel = Parcel.obtain()
        try {
            memory.writeToParcel(parcel, 0)
            parcel.setDataPosition(0)
            checkNotNull(parcel.readFileDescriptor()).use { send(it.fileDescriptor) }
        } finally {
            parcel.recycle()
        }
    }

    fun claim(): Int = busy.indexOfFirst { !it }.also { if (it >= 0) busy[it] = true }

    fun release(slot: Int) {
        if (slot in busy.indices) busy[slot] = false
    }

    fun rgba(slot: Int): ByteBuffer = rgbaSlots[slot]

    fun yv12(slot: Int): ByteBuffer = yv12Slots[slot]

    private fun slice(offset: Int, length: Int): ByteBuffer = bytes.duplicate().apply {
        position(offset)
        limit(offset + length)
    }.slice()

    override fun close() {
        SharedMemory.unmap(bytes)
        memory.close()
    }
}

internal data class TargetCameraVideoMode(val fps: Range<Int>, val constrained: Boolean) {
    companion object {
        const val WIDTH = 1920
        const val HEIGHT = 1080
        private val size = Size(WIDTH, HEIGHT)

        fun forCamera(camera: CameraCharacteristics): TargetCameraVideoMode? {
            val streams = camera.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP) ?: return null
            if (size in streams.getOutputSizes(SurfaceTexture::class.java).orEmpty() &&
                size in streams.getOutputSizes(MediaRecorder::class.java).orEmpty()
            ) {
                val duration = streams.getOutputMinFrameDuration(SurfaceTexture::class.java, size)
                val fps = camera.get(CameraCharacteristics.CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES)
                    ?.firstOrNull { it.lower == 60 && it.upper == 60 }
                if (fps != null && duration in 1..16_666_667) return TargetCameraVideoMode(fps, false)
            }
            if (size !in streams.highSpeedVideoSizes) return null
            val fps = streams.getHighSpeedVideoFpsRangesFor(size)
                .firstOrNull { it.lower == 120 && it.upper == 120 } ?: return null
            return TargetCameraVideoMode(fps, true)
        }
    }
}

/** Draw the live Camera2 frame into the Guest preview and encoder formats on the camera handler. */
internal class TargetCameraVideoFrames(
    private val memory: TargetCameraVideoMemory,
    private val shouldRead: () -> Boolean,
    private val onFrame: (Long, Int) -> Unit,
    private val onFailure: (Throwable) -> Unit,
) : AutoCloseable {
    private var display: EGLDisplay = EGL14.EGL_NO_DISPLAY
    private var context: EGLContext = EGL14.EGL_NO_CONTEXT
    private var output: EGLSurface = EGL14.EGL_NO_SURFACE
    private var texture: SurfaceTexture? = null
    private var input: Surface? = null
    private data class Program(val id: Int, val position: Int, val matrix: Int)
    private lateinit var previewProgram: Program
    private lateinit var videoProgram: Program
    private var textureName = 0
    private val transform = FloatArray(16)
    private val quad = ByteBuffer.allocateDirect(32).order(ByteOrder.nativeOrder()).asFloatBuffer().apply {
        put(floatArrayOf(0f, 0f, 1f, 0f, 0f, 1f, 1f, 1f))
        position(0)
    }
    private var lastTimestamp = 0L
    private var loggedGeometry = false

    fun open(handler: Handler): Surface {
        display = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY)
        val version = IntArray(2)
        check(EGL14.eglInitialize(display, version, 0, version, 1)) { "camera eglInitialize" }
        val configs = arrayOfNulls<android.opengl.EGLConfig>(1)
        val count = IntArray(1)
        val config = intArrayOf(
            EGL14.EGL_SURFACE_TYPE, EGL14.EGL_PBUFFER_BIT,
            EGL14.EGL_RENDERABLE_TYPE, EGL14.EGL_OPENGL_ES2_BIT,
            EGL14.EGL_RED_SIZE, 8, EGL14.EGL_GREEN_SIZE, 8, EGL14.EGL_BLUE_SIZE, 8,
            EGL14.EGL_ALPHA_SIZE, 8, EGL14.EGL_NONE,
        )
        check(EGL14.eglChooseConfig(display, config, 0, configs, 0, 1, count, 0) && count[0] == 1) {
            "camera eglChooseConfig"
        }
        context = EGL14.eglCreateContext(
            display, configs[0], EGL14.EGL_NO_CONTEXT,
            intArrayOf(EGL14.EGL_CONTEXT_CLIENT_VERSION, 2, EGL14.EGL_NONE), 0,
        )
        output = EGL14.eglCreatePbufferSurface(
            display, configs[0], intArrayOf(EGL14.EGL_WIDTH, WIDTH, EGL14.EGL_HEIGHT, YV12_ROWS, EGL14.EGL_NONE), 0,
        )
        check(EGL14.eglMakeCurrent(display, output, output, context)) { "camera eglMakeCurrent" }
        previewProgram = createProgram(PREVIEW_FRAGMENT)
        videoProgram = createProgram(YV12_FRAGMENT)
        val names = IntArray(1)
        GLES20.glGenTextures(1, names, 0)
        textureName = names[0]
        GLES20.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, textureName)
        GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_MIN_FILTER, GLES20.GL_LINEAR)
        GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_MAG_FILTER, GLES20.GL_LINEAR)
        GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_WRAP_S, GLES20.GL_CLAMP_TO_EDGE)
        GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_WRAP_T, GLES20.GL_CLAMP_TO_EDGE)
        texture = SurfaceTexture(textureName).apply {
            setDefaultBufferSize(WIDTH, HEIGHT)
            setOnFrameAvailableListener({ frame ->
                if (frame === texture) runCatching { read(frame) }.onFailure(onFailure)
            }, handler)
        }
        return Surface(texture).also { input = it }
    }

    private fun createProgram(fragmentSource: String): Program {
        val id = GLES20.glCreateProgram()
        val vertex = shader(GLES20.GL_VERTEX_SHADER, VERTEX)
        val fragment = shader(GLES20.GL_FRAGMENT_SHADER, fragmentSource)
        GLES20.glAttachShader(id, vertex)
        GLES20.glAttachShader(id, fragment)
        GLES20.glLinkProgram(id)
        GLES20.glDeleteShader(vertex)
        GLES20.glDeleteShader(fragment)
        val status = IntArray(1)
        GLES20.glGetProgramiv(id, GLES20.GL_LINK_STATUS, status, 0)
        check(status[0] != 0) { GLES20.glGetProgramInfoLog(id) }
        return Program(id, GLES20.glGetAttribLocation(id, "point"), GLES20.glGetUniformLocation(id, "transform"))
    }

    private fun shader(type: Int, source: String): Int {
        val shader = GLES20.glCreateShader(type)
        GLES20.glShaderSource(shader, source)
        GLES20.glCompileShader(shader)
        val status = IntArray(1)
        GLES20.glGetShaderiv(shader, GLES20.GL_COMPILE_STATUS, status, 0)
        check(status[0] != 0) { GLES20.glGetShaderInfoLog(shader) }
        return shader
    }

    private fun read(texture: SurfaceTexture) {
        texture.updateTexImage()
        val timestamp = texture.timestamp
        if (!shouldRead() || timestamp <= lastTimestamp) return
        lastTimestamp = timestamp
        val slot = memory.claim()
        if (slot == -1) return
        texture.getTransformMatrix(transform)
        if (alignCameraVideoTexture(transform) && !loggedGeometry) {
            loggedGeometry = true
            android.util.Log.i("RmeCameraGeometry", "Restored the sensor axes of the 1080p video texture")
        }
        GLES20.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, textureName)
        draw(previewProgram, WIDTH, HEIGHT, memory.rgba(slot))
        draw(videoProgram, WIDTH / 4, YV12_ROWS, memory.yv12(slot))
        check(GLES20.glGetError() == GLES20.GL_NO_ERROR) { "camera glReadPixels" }
        onFrame(timestamp, slot)
    }

    private fun draw(program: Program, width: Int, height: Int, output: ByteBuffer) {
        GLES20.glViewport(0, 0, width, height)
        GLES20.glUseProgram(program.id)
        GLES20.glUniformMatrix4fv(program.matrix, 1, false, transform, 0)
        GLES20.glVertexAttribPointer(program.position, 2, GLES20.GL_FLOAT, false, 0, quad)
        GLES20.glEnableVertexAttribArray(program.position)
        GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4)
        GLES20.glDisableVertexAttribArray(program.position)
        GLES20.glReadPixels(0, 0, width, height, GLES20.GL_RGBA, GLES20.GL_UNSIGNED_BYTE, output)
    }

    override fun close() {
        val oldTexture = texture
        texture = null
        input?.release()
        input = null
        oldTexture?.release()
        if (display != EGL14.EGL_NO_DISPLAY) {
            EGL14.eglMakeCurrent(display, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_CONTEXT)
            if (output != EGL14.EGL_NO_SURFACE) EGL14.eglDestroySurface(display, output)
            if (context != EGL14.EGL_NO_CONTEXT) EGL14.eglDestroyContext(display, context)
            EGL14.eglTerminate(display)
            display = EGL14.EGL_NO_DISPLAY
        }
    }

    companion object {
        private const val WIDTH = TargetCameraVideoMode.WIDTH
        private const val HEIGHT = TargetCameraVideoMode.HEIGHT
        private const val YV12_ROWS = HEIGHT * 3 / 2
        private const val VERTEX = """
            attribute vec2 point;
            uniform highp mat4 transform;
            varying highp vec2 uv;
            void main() {
                gl_Position = vec4(point * 2.0 - 1.0, 0.0, 1.0);
                uv = (transform * vec4(point.x, 1.0 - point.y, 0.0, 1.0)).xy;
            }
        """
        private const val PREVIEW_FRAGMENT = """#extension GL_OES_EGL_image_external : require
            precision highp float;
            uniform samplerExternalOES tex;
            varying vec2 uv;
            void main() { gl_FragColor = texture2D(tex, uv); }
        """
        // Four consecutive samples are written into each RGBA pixel. One packed row is
        // a full luma row or two chroma rows. Reading the rectangle yields Y, then V, then U.
        private const val YV12_FRAGMENT = """#extension GL_OES_EGL_image_external : require
            precision highp float;
            uniform samplerExternalOES tex;
            uniform highp mat4 transform;
            const float width = $WIDTH.0;
            const float height = $HEIGHT.0;
            vec3 sampleRgb(vec2 pixel) {
                vec2 q = vec2(pixel.x / width, 1.0 - pixel.y / height);
                return texture2D(tex, (transform * vec4(q, 0.0, 1.0)).xy).rgb;
            }
            float component(vec2 pixel, vec3 weights) {
                return dot(sampleRgb(pixel), weights);
            }
            void main() {
                vec2 p = floor(gl_FragCoord.xy);
                vec3 weights = vec3(0.2126, 0.7152, 0.0722) * (219.0 / 255.0);
                float offset = 16.0 / 255.0;
                float delta = 1.0;
                vec2 origin = vec2(p.x * 4.0 + 0.5, p.y + 0.5);
                if (p.y >= height) {
                    bool cr = p.y < height * 1.25;
                    weights = (cr ? vec3(0.5, -0.454153, -0.045847)
                                  : vec3(-0.114572, -0.385428, 0.5)) * (224.0 / 255.0);
                    offset = 128.0 / 255.0;
                    delta = 2.0;
                    float halfRow = step(width / 8.0, p.x);
                    float row = (p.y - (cr ? height : height * 1.25)) * 2.0 + halfRow;
                    origin = vec2(mod(p.x, width / 8.0) * 8.0 + 1.0, row * 2.0 + 1.0);
                }
                gl_FragColor = vec4(
                    component(origin, weights),
                    component(origin + vec2(delta, 0.0), weights),
                    component(origin + vec2(delta * 2.0, 0.0), weights),
                    component(origin + vec2(delta * 3.0, 0.0), weights)
                ) + offset;
            }
        """
    }
}
