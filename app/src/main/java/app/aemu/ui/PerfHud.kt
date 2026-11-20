package app.aemu.ui

import android.app.ActivityManager
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.BatteryManager
import android.os.Debug
import android.os.Process
import android.os.SystemClock
import java.io.File

/**
 * The performance overlay's numbers, sampled once a second: the guest screen's frame rate and touch latency (GuestScreen),
 * CPU and memory of every process of this app's user (the :vm process, qemu and the guest's programs all run as it),
 * the phone's free memory, CPU clock and battery temperature. Each item can be switched off; the overlay draws only
 * the chosen ones and reads only what they need.
 */
internal class PerfHud(private val ctx: Context) {
    enum class Item(val key: String) { FPS("fps"), TOUCH("touch"), CPU("cpu"), RAM("ram"), PHONE_RAM("phone_ram"), PROCS("procs"), FREQ("freq"), TEMP("temp"), UPTIME("uptime") }

    private val uid = Process.myUid()
    private val activityManager = ctx.getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager
    private val cores = Runtime.getRuntime().availableProcessors().coerceAtLeast(1)
    private val hz = 100.0                       // USER_HZ: clock ticks in /proc/<pid>/stat
    private val pageKb = 4L                      // statm counts pages; Android arm64 kernels use 4K or 16K pages
        .let { runCatching { android.system.Os.sysconf(android.system.OsConstants._SC_PAGESIZE) / 1024 }.getOrDefault(it) }
    private var lastTicks = -1L
    private var lastAt = 0L
    private var lastMemoryAt = -5000L
    private var lastMemory: HostMemoryAccounting.Reading? = null

    data class Sample(val cpuPct: Double, val memory: HostMemoryAccounting.Reading?, val procs: Int)

    /** CPU (percent of the whole phone) and resident memory of this app's processes */
    fun ours(includeMemory: Boolean): Sample {
        val memory = if (includeMemory && SystemClock.elapsedRealtime() - lastMemoryAt >= 5000) {
            HostMemoryAccounting(androidResidentMemoryKiB())
        } else null
        var ticks = 0L; var n = 0
        File("/proc").listFiles()?.forEach { d ->
            val pid = d.name.toIntOrNull() ?: return@forEach
            runCatching {
                if (android.system.Os.stat(d.path).st_uid != uid) return@forEach
                val stat = File(d, "stat").readText()
                val f = stat.substring(stat.lastIndexOf(')') + 2).split(' ')
                ticks += f[11].toLong() + f[12].toLong()               // utime, stime
                n++
                if (memory != null) {
                    val resident = File(d, "statm").readText().split(' ')[1].toLong() * pageKb
                    memory.record(pid, resident) {
                        runCatching {
                            File(d, "smaps_rollup").useLines { lines ->
                                lines.firstOrNull { it.startsWith("Pss:") }
                                    ?.substringAfter(':')?.trim()?.substringBefore(' ')?.toLong()
                            }
                        }.getOrNull()
                    }
                }
            }
        }
        val now = SystemClock.elapsedRealtime()
        val pct = if (lastTicks >= 0 && now > lastAt) ((ticks - lastTicks) / hz) / ((now - lastAt) / 1000.0) / cores * 100 else 0.0
        lastTicks = ticks; lastAt = now
        if (memory != null) {
            lastMemory = memory.reading()
            lastMemoryAt = now
        }
        return Sample(pct.coerceIn(0.0, 100.0), if (includeMemory) lastMemory else null, n)
    }

    /** Android includes memtrack allocations outside smaps in these three resident fields. */
    private fun Debug.MemoryInfo.residentKiB(): Long = dalvikPss.toLong() + nativePss + otherPss

    private fun androidResidentMemoryKiB(): Map<Int, Long> {
        val self = Process.myPid()
        val pids = activityManager.runningAppProcesses.orEmpty()
            .filter { it.uid == uid && it.pid != self }
            .map { it.pid }
        return buildMap {
            if (pids.isNotEmpty()) {
                val memory = activityManager.getProcessMemoryInfo(pids.toIntArray())
                for ((pid, info) in pids.zip(memory.asList())) put(pid, info.residentKiB())
            }
            // The VM is this process. A self sample avoids ActivityManager's cache for its GPU usage.
            val current = Debug.MemoryInfo()
            Debug.getMemoryInfo(current)
            put(self, current.residentKiB())
        }
    }

    fun phoneFreeMb(): Long {
        val mi = ActivityManager.MemoryInfo(); activityManager.getMemoryInfo(mi)
        return mi.availMem / (1024 * 1024)
    }

    /** the fastest core's current clock, MHz (0 when the phone hides it) */
    fun maxFreqMhz(): Long = (0 until cores).maxOfOrNull { c ->
        runCatching { File("/sys/devices/system/cpu/cpu$c/cpufreq/scaling_cur_freq").readText().trim().toLong() / 1000 }.getOrDefault(0L)
    } ?: 0L

    fun batteryTempC(): Double? {
        val i = ctx.registerReceiver(null, IntentFilter(Intent.ACTION_BATTERY_CHANGED)) ?: return null
        val t = i.getIntExtra(BatteryManager.EXTRA_TEMPERATURE, Int.MIN_VALUE)
        return if (t == Int.MIN_VALUE) null else t / 10.0
    }
}

internal class HostMemoryAccounting(private val androidResidentKiB: Map<Int, Long>) {
    data class Reading(val mib: Long, val proportional: Boolean)

    private var rssKiB = 0L
    private var residentKiB = 0L
    private var complete = true

    fun record(pid: Int, rss: Long, readPss: () -> Long?) {
        rssKiB += rss
        if (rss == 0L || !complete) return
        // Use either the Android or the proc measurement for this PID. Both contain its mapped pages.
        val resident = androidResidentKiB[pid]?.takeIf { it > 0 } ?: readPss()
        if (resident == null) complete = false else residentKiB += resident
    }

    fun reading(): Reading? = if (rssKiB == 0L) null else Reading(
        (if (complete) residentKiB else rssKiB) / 1024,
        complete,
    )
}
