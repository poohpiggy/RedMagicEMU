package app.aemu.core

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.location.Location
import android.location.LocationListener
import android.location.LocationManager
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.system.ErrnoException
import android.system.Os
import android.system.OsConstants
import androidx.core.content.ContextCompat
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
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

    fun isFresh(elapsedRealtimeNanos: Long, nowNanos: Long): Boolean =
        elapsedRealtimeNanos > 0 && elapsedRealtimeNanos in (nowNanos - MAX_FIX_AGE_NANOS)..nowNanos

    private const val MAX_FIX_AGE_NANOS = 30_000_000_000L
}

internal object HostLocationDemand {
    private const val MAX_LEASE_MILLIS = 6_000L
    private val active = Regex("1 ([1-9][0-9]{0,18})\n")

    fun isActive(text: String, nowMillis: Long): Boolean {
        val expiresAt = active.matchEntire(text)?.groupValues?.get(1)?.toLongOrNull() ?: return false
        return expiresAt - nowMillis in 1L..MAX_LEASE_MILLIS
    }
}

internal fun hostLocationCanListen(
    running: Boolean,
    visible: Boolean,
    enabled: Boolean,
    demanded: Boolean,
    permission: Boolean,
): Boolean = running && visible && enabled && demanded && permission

/** Shares recent host fixes with the Guest while a location request is active. */
class HostLocationBridge(
    private val context: Context,
    root: File,
    private val log: (String) -> Unit,
    initialBridgeEnabled: Boolean = true,
) {
    private val file = File(root, "dev/aemu_location")
    private val demandFile = File(root, "dev/aemu_location_demand")
    private val lock = Any()
    private val demandHandler = lazy { Handler(Looper.getMainLooper()) }
    private val checkDemand = Runnable { synchronized(lock) { refreshLocked() } }
    private val mutablePermissionNeeded = MutableStateFlow(false)
    internal val permissionNeeded: StateFlow<Boolean> = mutablePermissionNeeded

    private var registration: Pair<LocationManager, LocationListener>? = null
    private var bridgeEnabled = initialBridgeEnabled
    private var running = false
    private var visible = false
    private var demandReady = false
    private var last: HostLocationReading? = null
    private var lastProblem: String? = null

    fun start() = synchronized(lock) {
        if (running) return@synchronized
        running = true
        demandReady = try {
            Files.deleteIfExists(demandFile.toPath())
            publish(null)
        } catch (failure: java.io.IOException) {
            problem("stale demand could not be removed: ${failure.message}")
            false
        }
        if (demandReady) last = null
        refreshLocked()
    }

    fun stop() = synchronized(lock) {
        running = false
        demandReady = false
        refreshLocked()
    }

    fun resume(on: Boolean) = synchronized(lock) {
        visible = on
        refreshLocked()
    }

    fun setBridgeEnabled(enabled: Boolean) = synchronized(lock) {
        if (bridgeEnabled == enabled) return@synchronized
        bridgeEnabled = enabled
        refreshLocked()
    }

    fun permissionChanged() = synchronized(lock) { refreshLocked() }

    internal fun shouldAskPermission(): Boolean = synchronized(lock) {
        mutablePermissionNeeded.value && hasActiveDemand() && !permissionGranted()
    }

    private fun refreshLocked() {
        if (demandHandler.isInitialized()) demandHandler.value.removeCallbacks(checkDemand)
        val watching = running && visible && bridgeEnabled && demandReady
        val demanded = watching && hasActiveDemand()
        val granted = demanded && permissionGranted()
        if (hostLocationCanListen(running, visible, bridgeEnabled, demanded, granted)) {
            startListeningLocked()
        } else {
            unregisterLocked()
            if (last != null && publish(null)) last = null
        }
        if (watching) demandHandler.value.postDelayed(checkDemand, 1_000L)
        mutablePermissionNeeded.value = demanded && !granted
    }

    private fun hasActiveDemand(): Boolean {
        return try {
            val fd = Os.open(
                demandFile.absolutePath,
                OsConstants.O_RDONLY or OsConstants.O_CLOEXEC or OsConstants.O_NOFOLLOW or OsConstants.O_NONBLOCK,
                0,
            )
            try {
                if (!OsConstants.S_ISREG(Os.fstat(fd).st_mode)) return false
                val bytes = ByteArray(23) // One byte beyond the longest valid lease.
                val count = Os.read(fd, bytes, 0, bytes.size)
                HostLocationDemand.isActive(String(bytes, 0, count, Charsets.US_ASCII), SystemClock.elapsedRealtime())
            } finally {
                Os.close(fd)
            }
        } catch (failure: ErrnoException) {
            if (failure.errno != OsConstants.ENOENT) {
                problem("cannot read Guest location demand: ${failure.message}")
            }
            false
        } catch (_: java.io.InterruptedIOException) {
            false
        }
    }

    @Suppress("DEPRECATION")
    private fun startListeningLocked() {
        if (registration != null) return
        val lm = context.getSystemService(LocationManager::class.java)
        if (lm == null) {
            problem("LocationManager unavailable")
            return
        }
        val listener = object : LocationListener {
            override fun onLocationChanged(location: Location) = accept(this, location)

            @Suppress("OVERRIDE_DEPRECATION")
            override fun onStatusChanged(provider: String?, status: Int, extras: Bundle?) {}
            override fun onProviderEnabled(provider: String) {}
            override fun onProviderDisabled(provider: String) {}
        }
        val providers = listOf(LocationManager.NETWORK_PROVIDER, LocationManager.GPS_PROVIDER)
        var failure: Exception? = null
        val registered = providers.filter { provider ->
            try {
                if (!lm.isProviderEnabled(provider)) return@filter false
                // Request timed updates even while stationary so Guest one-shot clients can get a fresh fix.
                lm.requestLocationUpdates(provider, 5_000L, 0f, listener, demandHandler.value.looper)
                true
            } catch (error: Exception) {
                failure = error
                false
            }
        }
        if (registered.isEmpty()) {
            problem(failure?.let { "${it.javaClass.simpleName}: ${it.message}" } ?: "no enabled host location provider")
            return
        }
        registration = lm to listener
        problem(null)
        log("location: host provider registered")
        for (provider in registered) {
            try {
                lm.getLastKnownLocation(provider)?.let { accept(listener, it) }
            } catch (error: Exception) {
                problem("cannot read cached $provider fix: ${error.javaClass.simpleName}")
            }
        }
    }

    private fun permissionGranted(): Boolean =
        ContextCompat.checkSelfPermission(context, Manifest.permission.ACCESS_COARSE_LOCATION) ==
            PackageManager.PERMISSION_GRANTED ||
            ContextCompat.checkSelfPermission(context, Manifest.permission.ACCESS_FINE_LOCATION) ==
            PackageManager.PERMISSION_GRANTED

    @Suppress("DEPRECATION")
    private fun unregisterLocked() {
        val (manager, listener) = registration ?: return
        registration = null
        try {
            manager.removeUpdates(listener)
            log("location: host provider released")
        } catch (failure: Exception) {
            problem("cannot release host provider: ${failure.javaClass.simpleName}")
        }
    }

    private fun accept(listener: LocationListener, location: Location) {
        if (!location.hasAccuracy()) return
        val reading = HostLocationProtocol.reading(
            location.latitude,
            location.longitude,
            location.altitude.takeIf { location.hasAltitude() },
            location.accuracy,
            location.elapsedRealtimeNanos,
            location.time,
        ) ?: return
        synchronized(lock) {
            if (registration?.second !== listener) return
            if (!hasActiveDemand() || !permissionGranted()) return
            if (!HostLocationProtocol.isFresh(reading.elapsedRealtimeNanos, SystemClock.elapsedRealtimeNanos()) ||
                reading.elapsedRealtimeNanos <= (last?.elapsedRealtimeNanos ?: 0L)) return
            if (publish(reading)) {
                last = reading
                problem(null)
            }
        }
    }

    private fun publish(reading: HostLocationReading?): Boolean {
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
            return true
        } catch (failure: Exception) {
            problem("publish failed: ${failure.message}")
            return false
        }
    }

    private fun problem(message: String?) {
        if (message == lastProblem) return
        lastProblem = message
        if (message != null) log("location: $message")
    }
}
