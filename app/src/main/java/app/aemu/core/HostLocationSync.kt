package app.aemu.core

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.location.Location
import android.location.LocationListener
import android.location.LocationManager
import android.os.Bundle
import android.os.SystemClock
import androidx.core.content.ContextCompat
import java.io.File
import java.nio.file.Files
import java.nio.file.StandardCopyOption
import java.util.Locale

internal data class HostLocationReading(
    val latitude: Double,
    val longitude: Double,
    val altitude: Double?,
    val accuracy: Float,
    val elapsedRealtimeNanos: Long,
    val timeMillis: Long,
) {
    fun encode(): String = String.format(
        Locale.US,
        "1 %.8f %.8f %s %.3f %d %d\n",
        latitude,
        longitude,
        altitude?.toString() ?: "null",
        accuracy,
        elapsedRealtimeNanos,
        timeMillis,
    )
}

internal object HostLocationProtocol {
    fun reading(
        latitude: Double,
        longitude: Double,
        altitude: Double?,
        accuracy: Float,
        elapsedRealtimeNanos: Long,
        timeMillis: Long,
    ): HostLocationReading? {
        if (!latitude.isFinite() || latitude !in -90.0..90.0) return null
        if (!longitude.isFinite() || longitude !in -180.0..180.0) return null
        if (altitude != null && !altitude.isFinite()) return null
        if (!accuracy.isFinite() || accuracy < 0f) return null
        if (elapsedRealtimeNanos <= 0L || timeMillis <= 0L) return null
        return HostLocationReading(latitude, longitude, altitude, accuracy, elapsedRealtimeNanos, timeMillis)
    }

    fun parse(text: String): HostLocationReading? {
        val parts = text.trim().split(Regex("\\s+"))
        if (parts.size != 7 || parts[0] != "1") return null
        return reading(
            parts[1].toDoubleOrNull() ?: return null,
            parts[2].toDoubleOrNull() ?: return null,
            if (parts[3] == "null") null else parts[3].toDoubleOrNull() ?: return null,
            parts[4].toFloatOrNull() ?: return null,
            parts[5].toLongOrNull() ?: return null,
            parts[6].toLongOrNull() ?: return null,
        )
    }
}

internal fun hostLocationCanListen(running: Boolean, visible: Boolean, enabled: Boolean, permission: Boolean): Boolean =
    running && visible && enabled && permission

/** Publishes the host's last known location; the Guest GPS provider reads it from dev/aemu_location. */
class HostLocationBridge(
    private val context: Context,
    private val root: File,
    private val log: (String) -> Unit,
    initialBridgeEnabled: Boolean = true,
) : LocationListener {
    private val file = File(root, "dev/aemu_location")
    private val lock = Any()
    private var manager: LocationManager? = null
    private var listening = false
    private var bridgeEnabled = initialBridgeEnabled
    private var running = false
    private var visible = false
    private var last: HostLocationReading? = null
    private var lastProblem: String? = null

    fun start() = synchronized(lock) {
        running = true
        last = null
        publish(null)
        apply()
    }

    fun stop() = synchronized(lock) {
        running = false
        visible = false
        unregisterLocked()
        publish(null)
    }

    fun resume(on: Boolean) = synchronized(lock) {
        visible = on
        apply()
    }

    fun setBridgeEnabled(enabled: Boolean) = synchronized(lock) {
        if (bridgeEnabled == enabled) return@synchronized
        bridgeEnabled = enabled
        last = null
        if (!enabled) publish(null)
        apply()
    }

    fun permissionChanged() = synchronized(lock) { apply() }

    @Suppress("DEPRECATION")
    private fun apply() {
        if (!hostLocationCanListen(running, visible, bridgeEnabled, permissionGranted())) {
            unregisterLocked()
            return
        }
        if (listening) return
        val lm = context.getSystemService(LocationManager::class.java)
        if (lm == null) {
            problem("LocationManager unavailable")
            return
        }
        manager = lm
        runCatching {
            val providers = listOf(LocationManager.NETWORK_PROVIDER, LocationManager.GPS_PROVIDER)
            var registered = false
            for (provider in providers) {
                if (runCatching { lm.isProviderEnabled(provider) }.getOrDefault(false).not()) continue
                runCatching { lm.getLastKnownLocation(provider) }.getOrNull()?.let(::onLocationChanged)
                if (runCatching { lm.requestLocationUpdates(provider, 5_000L, 5f, this) }.isSuccess)
                    registered = true
            }
            check(registered) { "no enabled host location provider" }
            listening = true
            problem(null)
            log("location: host provider registered")
        }.onFailure {
            unregisterLocked()
            problem("${it.javaClass.simpleName}: ${it.message}")
        }
    }

    private fun permissionGranted(): Boolean =
        ContextCompat.checkSelfPermission(context, Manifest.permission.ACCESS_COARSE_LOCATION) ==
            PackageManager.PERMISSION_GRANTED ||
            ContextCompat.checkSelfPermission(context, Manifest.permission.ACCESS_FINE_LOCATION) ==
            PackageManager.PERMISSION_GRANTED

    @Suppress("DEPRECATION")
    private fun unregisterLocked() {
        if (listening) runCatching { manager?.removeUpdates(this) }
        listening = false
        manager = null
    }

    override fun onLocationChanged(location: Location) {
        val reading = HostLocationProtocol.reading(
            location.latitude,
            location.longitude,
            location.altitude.takeIf { location.hasAltitude() },
            if (location.hasAccuracy()) location.accuracy else 0f,
            location.elapsedRealtimeNanos.takeIf { it > 0L } ?: SystemClock.elapsedRealtimeNanos(),
            location.time.takeIf { it > 0L } ?: System.currentTimeMillis(),
        ) ?: return
        synchronized(lock) {
            if (!running || !visible || reading == last) return
            publish(reading)
            last = reading
            problem(null)
        }
    }

    @Deprecated("Deprecated in Android 30")
    override fun onStatusChanged(provider: String?, status: Int, extras: Bundle?) {}

    override fun onProviderEnabled(provider: String) {}

    override fun onProviderDisabled(provider: String) {}

    private fun publish(reading: HostLocationReading?) {
        try {
            val dir = file.parentFile ?: throw IllegalArgumentException("location file has no parent")
            if (!dir.isDirectory && !dir.mkdirs()) throw java.io.IOException("location directory unavailable")
            val tmp = Files.createTempFile(dir.toPath(), ".aemu-location-", ".tmp")
            try {
                Files.write(tmp, (reading?.encode() ?: "0\n").toByteArray(Charsets.US_ASCII))
                if (!tmp.toFile().setReadable(true, false)) throw java.io.IOException("location file cannot be read by guest")
                Files.move(tmp, file.toPath(), StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING)
            } finally {
                Files.deleteIfExists(tmp)
            }
        } catch (failure: Exception) {
            problem("publish failed: ${failure.message}")
        }
    }

    private fun problem(message: String?) {
        if (message == lastProblem) return
        lastProblem = message
        if (message != null) log("location: $message")
    }
}
