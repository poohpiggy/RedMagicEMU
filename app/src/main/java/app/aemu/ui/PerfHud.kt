package app.aemu.ui

import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.BatteryManager
import android.os.Process
import android.os.SystemClock
import java.io.File

/**
 * The performance overlay's numbers, sampled once a second: the guest screen's frame rate and touch latency (GuestScreen),
 * CPU and memory of every process of this app's user (the :vm process, qemu and the guest's programs all run as it),
 * the phone's free memory, CPU clock and battery temperature. Each item can be switched off; the overlay draws only
 * the chosen ones and reads only what they need.
 */
class PerfHud(private val ctx: Context) {
    enum class Item(val key: String) { FPS("fps"), TOUCH("touch"), CPU("cpu"), RAM("ram"), PHONE_RAM("phone_ram"), PROCS("procs"), FREQ("freq"), TEMP("temp"), UPTIME("uptime") }

    private val uid = Process.myUid()
    private val cores = Runtime.getRuntime().availableProcessors().coerceAtLeast(1)
    private val hz = 100.0                       // USER_HZ: clock ticks in /proc/<pid>/stat
    private val pageKb = 4L                      // statm counts pages; Android arm64 kernels use 4K or 16K pages
        .let { runCatching { android.system.Os.sysconf(android.system.OsConstants._SC_PAGESIZE) / 1024 }.getOrDefault(it) }
    private var lastTicks = -1L
    private var lastAt = 0L

    data class Sample(val cpuPct: Double, val rssMb: Long, val procs: Int)

    /** CPU (percent of the whole phone) and resident memory of this app's processes */
    fun ours(): Sample {
        var ticks = 0L; var rss = 0L; var n = 0
        File("/proc").listFiles()?.forEach { d ->
            val pid = d.name.toIntOrNull() ?: return@forEach
            runCatching {
                if (android.system.Os.stat(d.path).st_uid != uid) return@forEach
                val stat = File(d, "stat").readText()
                val f = stat.substring(stat.lastIndexOf(')') + 2).split(' ')
                ticks += f[11].toLong() + f[12].toLong()               // utime, stime
                rss += File(d, "statm").readText().split(' ')[1].toLong() * pageKb
                n++
            }
        }
        val now = SystemClock.elapsedRealtime()
        val pct = if (lastTicks >= 0 && now > lastAt) ((ticks - lastTicks) / hz) / ((now - lastAt) / 1000.0) / cores * 100 else 0.0
        lastTicks = ticks; lastAt = now
        return Sample(pct.coerceIn(0.0, 100.0), rss / 1024, n)
    }

    fun phoneFreeMb(): Long {
        val am = ctx.getSystemService(Context.ACTIVITY_SERVICE) as android.app.ActivityManager
        val mi = android.app.ActivityManager.MemoryInfo(); am.getMemoryInfo(mi)
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
