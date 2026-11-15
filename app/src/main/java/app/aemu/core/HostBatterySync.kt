package app.aemu.core

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.BatteryManager
import androidx.core.content.ContextCompat

internal data class HostBatteryReading(val level: Int, val status: Int, val plugged: Int) {
    fun command(): String {
        val ac = if (plugged and BatteryManager.BATTERY_PLUGGED_AC != 0) 1 else 0
        val usb = if (plugged and BatteryManager.BATTERY_PLUGGED_USB != 0) 1 else 0
        val wireless = if (plugged and BatteryManager.BATTERY_PLUGGED_WIRELESS != 0) 1 else 0
        return "dumpsys battery set level $level; dumpsys battery set status $status; " +
            "dumpsys battery set ac $ac; dumpsys battery set usb $usb; dumpsys battery set wireless $wireless"
    }
}

internal object HostBatteryProtocol {
    fun reading(level: Int, status: Int, plugged: Int): HostBatteryReading? {
        if (level !in 0..100 || status !in BatteryManager.BATTERY_STATUS_UNKNOWN..BatteryManager.BATTERY_STATUS_FULL) return null
        if (plugged !in 0..15) return null
        return HostBatteryReading(level, status, plugged)
    }
}

/** Mirrors the host's current battery into the guest once the guest is running. */
internal class HostBatteryBridge(
    private val context: Context,
    private val running: () -> Boolean,
    private val runGuest: (String, Long) -> String,
    private val log: (String) -> Unit,
) {
    private val lock = Any()
    private var receiver: BroadcastReceiver? = null
    private var latest: HostBatteryReading? = null
    private var applied: HostBatteryReading? = null
    private var workerRunning = false
    private var forcePending = false
    private var lastProblem: String? = null
    private var closed = false

    fun start() {
        val rx = object : BroadcastReceiver() {
            override fun onReceive(context: Context, intent: Intent) {
                val reading = HostBatteryProtocol.reading(
                    intent.getIntExtra(BatteryManager.EXTRA_LEVEL, -1),
                    intent.getIntExtra(BatteryManager.EXTRA_STATUS, BatteryManager.BATTERY_STATUS_UNKNOWN),
                    intent.getIntExtra(BatteryManager.EXTRA_PLUGGED, -1),
                ) ?: return
                synchronized(lock) {
                    if (closed || (reading == latest && lastProblem == null)) return
                    latest = reading
                }
                schedule()
            }
        }
        receiver = rx
        ContextCompat.registerReceiver(
            context,
            rx,
            IntentFilter(Intent.ACTION_BATTERY_CHANGED),
            ContextCompat.RECEIVER_NOT_EXPORTED,
        )
    }

    fun vmRunning() = schedule()

    fun forceRefresh() {
        synchronized(lock) { forcePending = true }
        schedule()
    }

    fun stop() {
        val rx = synchronized(lock) {
            closed = true
            receiver.also { receiver = null }
        }
        if (rx != null) runCatching { context.unregisterReceiver(rx) }
    }

    private fun schedule() {
        synchronized(lock) {
            val current = latest
            if (closed || workerRunning || current == null || (current == applied && !forcePending) || !running()) return
            workerRunning = true
        }
        Thread({ drain() }, "host-battery-sync").apply { isDaemon = true }.start()
    }

    private fun drain() {
        while (true) {
            val next = synchronized(lock) {
                val current = latest
                if (closed || current == null || (current == applied && !forcePending) || !running()) {
                    workerRunning = false
                    null
                } else {
                    forcePending = false
                    current
                }
            } ?: return
            try {
                runGuest(next.command(), 30_000)
            } catch (failure: Exception) {
                synchronized(lock) {
                    val problem = failure.javaClass.simpleName + ": " + failure.message
                    if (problem != lastProblem) log("battery: host state delivery failed ($problem)")
                    lastProblem = problem
                    workerRunning = false
                }
                return
            }
            synchronized(lock) {
                applied = next
                lastProblem = null
            }
        }
    }
}
