package app.aemu.core

import android.content.Context
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import android.graphics.Point
import android.os.VibrationEffect
import android.os.Vibrator
import android.os.SystemClock
import android.system.ErrnoException
import android.system.Os
import android.system.OsConstants
import android.util.AtomicFile
import android.view.WindowManager
import java.io.File
import java.io.FileDescriptor
import java.io.FileInputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder

/** The old vibration FIFO requires explicit opt-in. */
internal class SensorChannels(
    private val legacyVibration: Boolean,
    private val accelerometerStart: () -> Unit,
    private val accelerometerStop: () -> Unit,
    private val vibrationStart: () -> Unit,
    private val vibrationStop: () -> Unit,
) {
    fun start() {
        accelerometerStart()
        if (legacyVibration) vibrationStart()
    }

    fun stop() {
        accelerometerStop()
        if (legacyVibration) vibrationStop()
    }
}

/** Report state transitions without logging the same failure for every sample. */
internal class SensorBridgeStatus(private val log: (String) -> Unit) {
    private var registration: String? = null
    private var writeProblem: String? = null
    private var consecutiveWrites = 0

    @Synchronized fun registration(state: String) {
        if (registration != state) {
            registration = state
            log("sensors: $state")
        }
    }

    @Synchronized fun writeFailure(wouldBlock: Boolean, error: String) {
        consecutiveWrites = 0
        val problem = if (wouldBlock) "congested" else "failed"
        if (writeProblem != problem) {
            writeProblem = problem
            if (wouldBlock) log("sensors: accelerometer channel congested; samples dropped")
            else log("sensors: accelerometer write failed: $error")
        }
    }

    @Synchronized fun writeSucceeded() {
        if (writeProblem != null && ++consecutiveWrites >= 50) {
            writeProblem = null
            consecutiveWrites = 0
            log("sensors: accelerometer write recovered")
        }
    }

    fun registrationAttempt(register: () -> Boolean): Boolean {
        return registrationAttempt("accelerometer", register)
    }

    fun registrationAttempt(name: String, register: () -> Boolean): Boolean {
        val registered = try {
            register()
        } catch (e: RuntimeException) {
            registration("$name listener registration failed: ${e.javaClass.simpleName}: ${e.message}")
            return false
        }
        registration(if (registered) "$name listener registered" else "$name listener registration failed")
        return registered
    }
}

internal class SensorBridgeGate(initialEnabled: Boolean) {
    @Volatile var enabled = initialEnabled
    fun canListen(running: Boolean, visible: Boolean, channelReady: Boolean, stateReady: Boolean): Boolean =
        enabled && running && visible && channelReady && stateReady
}

/**
 * The phone's motion sensors and vibration motor for the guest.
 *  - motion sensors: legacy accelerometer records, or typed motion records for NX809J/API 36
 *    into /dev/aemu_sensors;
 *    only while the VM screen is shown;
 *  - vibration: /sys/class/timed_output/vibrator/enable is a FIFO; libhardware_legacy / the vibrator HAL of 2.3–7.x
 *    write the duration in ms there, and the phone vibrates for that long. The FIFO requires explicit opt-in because
 *    it does not authenticate the Guest caller; disabling it does not retire a prior reader.
 */
class Sensors(private val ctx: Context, private val root: File, legacyVibration: Boolean, private val log: (String) -> Unit,
              private val targetProtocol: Boolean = false, initialBridgeEnabled: Boolean = true) : SensorEventListener {
    private val accelFifo = File(root, "dev/aemu_sensors")
    private val accelState = File(root, "dev/aemu_sensor_state")
    private val vibFifo = File(root, "sys/class/timed_output/vibrator/enable")
    @Volatile private var out: FileDescriptor? = null
    @Volatile private var running = false
    private val rec = ByteBuffer.allocate(16).order(ByteOrder.LITTLE_ENDIAN)
    private val status = SensorBridgeStatus(log)
    private val lifecycleLock = Any()
    private val bridgeGate = SensorBridgeGate(initialBridgeEnabled)
    val bridgeEnabled: Boolean get() = bridgeGate.enabled
    private var sessionStartNanos = 0L
    private val lastEventNanos = HashMap<Int, Long>()
    private var targetRotation = 0
    private data class OwnedState(val device: Long, val inode: Long, val uid: Int)
    private var ownedState: OwnedState? = null
    private var stateReady = !targetProtocol
    private val targetTypes = intArrayOf(Sensor.TYPE_ACCELEROMETER, Sensor.TYPE_GYROSCOPE, Sensor.TYPE_MAGNETIC_FIELD)
    private val optionalTypes = intArrayOf(Sensor.TYPE_GYROSCOPE_UNCALIBRATED, Sensor.TYPE_MAGNETIC_FIELD_UNCALIBRATED)
    @Volatile private var availableUncalibrated = emptyList<Int>()

    private fun sensorLabel(type: Int): String = when (type) {
        Sensor.TYPE_ACCELEROMETER -> "accelerometer"
        Sensor.TYPE_GYROSCOPE -> "gyroscope"
        Sensor.TYPE_MAGNETIC_FIELD -> "magnetometer"
        Sensor.TYPE_GYROSCOPE_UNCALIBRATED -> "uncalibrated gyroscope"
        Sensor.TYPE_MAGNETIC_FIELD_UNCALIBRATED -> "uncalibrated magnetometer"
        else -> "sensor $type"
    }

    private fun publishCapabilities(): List<Int> {
        val file = AtomicFile(File(root, "dev/.aemu-sensor-caps"))
        return try {
            val manager = ctx.getSystemService(SensorManager::class.java)
            val available = optionalTypes.filter { manager?.getDefaultSensor(it) != null }
            var mask = 0
            optionalTypes.forEachIndexed { index, type ->
                if (type in available) mask = mask or (1 shl index)
            }
            val stream = file.startWrite()
            try {
                stream.write("$mask\n".toByteArray(Charsets.US_ASCII))
                file.finishWrite(stream)
            } catch (failure: Exception) {
                file.failWrite(stream)
                throw failure
            }
            available
        } catch (failure: Exception) {
            file.delete()
            log("sensors: cannot publish sensor capabilities: ${failure.message}")
            emptyList()
        }
    }

    private fun resetSessionLocked() {
        sessionStartNanos = SystemClock.elapsedRealtimeNanos()
        lastEventNanos.clear()
    }

    @Suppress("DEPRECATION")
    private fun targetNaturalRotation(): Int {
        val display = ctx.getSystemService(WindowManager::class.java)?.defaultDisplay
        if (display == null) {
            log("sensors: host natural orientation unavailable; using identity axes")
            return 0
        }
        val size = Point()
        display.getRealSize(size)
        if (size.x <= 0 || size.y <= 0 || size.x == size.y) {
            log("sensors: host natural orientation ambiguous; using identity axes")
            return 0
        }
        val base = TargetAccelerometerProtocol.portraitRotation(size.x, size.y, display.rotation, false)
        if (base == 0) return 0
        // Android's WindowManager uses this framework bool to choose the natural-landscape device's default portrait.
        // It is an internal OEM-overridable resource, so the fallback is reported and needs device validation.
        val id = ctx.resources.getIdentifier("config_reverseDefaultRotation", "bool", "android")
        if (id == 0) {
            log("sensors: default portrait rotation unavailable; using 270-degree landscape mapping")
            return 270
        }
        val reversed = try { ctx.resources.getBoolean(id) } catch (e: android.content.res.Resources.NotFoundException) {
            log("sensors: default portrait rotation unavailable; using 270-degree landscape mapping")
            return 270
        }
        val rotation = TargetAccelerometerProtocol.portraitRotation(size.x, size.y, display.rotation, reversed)
        log("sensors: host natural landscape; fixed $rotation-degree portrait axis mapping")
        return rotation
    }

    private fun publishState(active: Boolean, timeNanos: Long): Boolean {
        if (!targetProtocol) return true
        return try {
            TargetAccelerometerState.publish(accelState, active, timeNanos)
            val stat = Os.lstat(accelState.absolutePath)
            if (!OsConstants.S_ISREG(stat.st_mode) || stat.st_uid != Os.getuid() || stat.st_nlink != 1L)
                throw java.io.IOException("published state file ownership invalid")
            ownedState = OwnedState(stat.st_dev, stat.st_ino, stat.st_uid)
            true
        } catch (e: Exception) {
            val invalidated = invalidateOwnedState()
            log("sensors: accelerometer state unavailable: ${e.message}; stale state ${if (invalidated) "invalidated" else "could not be invalidated"}")
            false
        }
    }

    /** Fail closed if rename failed but the exact state file published by this bridge is still writable. */
    private fun invalidateOwnedState(): Boolean {
        val owned = ownedState ?: return false
        return try {
            val fd = Os.open(accelState.absolutePath,
                OsConstants.O_RDWR or OsConstants.O_NOFOLLOW or OsConstants.O_CLOEXEC, 0)
            try {
                val stat = Os.fstat(fd)
                val bytes = ByteArray(64)
                val n = Os.pread(fd, bytes, 0, bytes.size, 0L)
                val content = if (n > 0) String(bytes, 0, n, Charsets.US_ASCII) else ""
                if (!TargetAccelerometerState.canInvalidate(owned.device, owned.inode, owned.uid,
                        stat.st_dev, stat.st_ino, stat.st_uid,
                        OsConstants.S_ISREG(stat.st_mode) && stat.st_nlink == 1L, content)) return false
                if (Os.pwrite(fd, byteArrayOf('0'.code.toByte()), 0, 1, 0L) != 1) return false
                Os.fsync(fd)
                true
            } finally { Os.close(fd) }
        } catch (e: Exception) {
            log("sensors: accelerometer state invalidation failed: ${e.message}")
            false
        }
    }

    private val channels = SensorChannels(legacyVibration, accelerometerStart = {
        runCatching {
            mkfifo(accelFifo)
            // O_RDWR | O_NONBLOCK: opening needs no reader, and a full pipe (guest not polling) drops samples
            synchronized(rec) {
                out = Os.open(accelFifo.absolutePath, OsConstants.O_RDWR or OsConstants.O_NONBLOCK or OsConstants.O_CLOEXEC, 0)
                resetSessionLocked()
            }
            apply()
        }.onFailure { log("sensors: accelerometer channel failed: ${it.message}") }
    }, accelerometerStop = {
        apply()
        synchronized(rec) { val fd = out; out = null; runCatching { fd?.let { Os.close(it) } } }
    }, vibrationStart = {
        runCatching {
            vibFifo.parentFile?.mkdirs()
            mkfifo(vibFifo)
            Thread({ vibrations() }, "vibrator").apply { isDaemon = true; start() }
        }.onFailure { log("sensors: vibrator channel failed: ${it.message}") }
    }, vibrationStop = {
        runCatching { File(vibFifo.absolutePath).appendText("0\n") }
    })

    fun start() = synchronized(lifecycleLock) {
        running = true
        if (targetProtocol) {
            targetRotation = targetNaturalRotation()
            availableUncalibrated = publishCapabilities()
        }
        stateReady = publishState(false, SystemClock.elapsedRealtimeNanos())
        channels.start()
    }

    fun stop() = synchronized(lifecycleLock) {
        publishState(false, SystemClock.elapsedRealtimeNanos())
        stateReady = false
        running = false
        channels.stop()
    }

    @Volatile private var visible = false

    fun setBridgeEnabled(enabled: Boolean) = synchronized(lifecycleLock) {
        if (bridgeGate.enabled == enabled) return@synchronized
        synchronized(rec) { bridgeGate.enabled = false }
        if (running) stateReady = publishState(false, SystemClock.elapsedRealtimeNanos())
        synchronized(rec) {
            resetSessionLocked()
            bridgeGate.enabled = enabled
        }
        apply()
    }

    /** The VM screen is visible: feed the accelerometer (about 50 Hz). Remembered until the channel is up. */
    fun resume(on: Boolean) = synchronized(lifecycleLock) {
        if (running) stateReady = publishState(false, SystemClock.elapsedRealtimeNanos())
        synchronized(rec) {
            visible = on
            if (on) resetSessionLocked()
        }
        apply()
    }

    private fun apply() {
        val sm = ctx.getSystemService(SensorManager::class.java)
        if (sm == null) {
            if (visible && running && out != null) status.registration("SensorManager unavailable")
            return
        }
        sm.unregisterListener(this)
        if (targetProtocol && !stateReady) {
            if (running) status.registration("accelerometer state publication failed")
            return
        }
        if (bridgeGate.canListen(running, visible, out != null, !targetProtocol || stateReady)) {
            val types = if (targetProtocol) targetTypes + availableUncalibrated else intArrayOf(Sensor.TYPE_ACCELEROMETER)
            var registered = 0
            for (type in types) {
                val sensor = sm.getDefaultSensor(type)
                if (sensor == null) {
                    status.registration("${sensorLabel(type)} unavailable")
                    continue
                }
                if (status.registrationAttempt(sensorLabel(type)) {
                        sm.registerListener(this, sensor, SensorManager.SENSOR_DELAY_GAME)
                    }) registered++
            }
            if (registered == 0) {
                status.registration("no motion sensor listener could be registered")
            } else if (targetProtocol && !publishState(true, sessionStartNanos)) {
                stateReady = false
                sm.unregisterListener(this)
                status.registration("motion sensor state publication failed")
            }
        }
    }

    override fun onSensorChanged(e: SensorEvent) {
        val fd = out ?: return
        val type = e.sensor.type
        val uncalibrated = targetProtocol && type in availableUncalibrated
        if (if (targetProtocol) type !in targetTypes && !uncalibrated else type != Sensor.TYPE_ACCELEROMETER) return
        if (uncalibrated && e.values.size < 6) return
        var disable = false
        synchronized(rec) {
            if (out !== fd || !bridgeGate.enabled || !running || !visible) return
            val previous = lastEventNanos[type] ?: 0L
            if (targetProtocol && !TargetAccelerometerProtocol.shouldForward(e.timestamp, sessionStartNanos, previous)) return
            if (targetProtocol) lastEventNanos[type] = e.timestamp
            rec.clear()
            rec.putInt(1).putFloat(e.values[0]).putFloat(e.values[1]).putFloat(e.values[2])
            val frame = if (uncalibrated) {
                TargetAccelerometerProtocol.encodeUncalibrated(type, e.timestamp, e.values, targetRotation)
            } else if (targetProtocol) {
                val axes = TargetAccelerometerProtocol.mapToPortrait(e.values[0], e.values[1], e.values[2], targetRotation)
                TargetAccelerometerProtocol.encode(type, e.timestamp, e.accuracy, axes[0], axes[1], axes[2])
            } else rec.array()
            try {
                val written = Os.write(fd, frame, 0, frame.size)
                if (written == frame.size) status.writeSucceeded()
                else {
                    // A short FIFO write destroys record alignment. Stop rather than append more corrupt frames.
                    status.writeFailure(false, "short write ($written/${frame.size}); channel disabled")
                    out = null
                    runCatching { Os.close(fd) }
                    disable = true
                }
            } catch (e: ErrnoException) {
                status.writeFailure(e.errno == OsConstants.EAGAIN, e.message ?: "errno ${e.errno}")
            }
        }
        if (disable) synchronized(lifecycleLock) { publishState(false, SystemClock.elapsedRealtimeNanos()); apply() }
    }

    override fun onAccuracyChanged(s: Sensor?, a: Int) {}

    private fun vibrations() {
        val fd = Os.open(vibFifo.absolutePath, OsConstants.O_RDWR or OsConstants.O_CLOEXEC, 0)
        val vib = ctx.getSystemService(Vibrator::class.java)
        val buf = ByteArray(64)
        FileInputStream(fd).use { s ->
            while (running) {
                val n = runCatching { s.read(buf) }.getOrDefault(-1)
                if (n <= 0) break
                val ms = String(buf, 0, n).trim().lines().lastOrNull()?.trim()?.toLongOrNull() ?: continue
                runCatching {
                    if (ms > 0) vib?.vibrate(VibrationEffect.createOneShot(ms.coerceAtMost(5_000), VibrationEffect.DEFAULT_AMPLITUDE)) else vib?.cancel()
                }
            }
        }
    }

    private fun mkfifo(f: File) {
        val isFifo = runCatching { OsConstants.S_ISFIFO(Os.lstat(f.absolutePath).st_mode) }.getOrDefault(false)
        if (isFifo) return
        f.delete(); f.parentFile?.mkdirs()
        Os.mkfifo(f.absolutePath, "666".toInt(8))
        Os.chmod(f.absolutePath, "666".toInt(8))
    }
}
