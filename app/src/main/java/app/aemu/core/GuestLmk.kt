package app.aemu.core

import android.net.LocalServerSocket
import android.net.LocalSocket
import android.net.LocalSocketAddress
import android.system.Os
import android.system.OsConstants
import java.io.File
import java.io.FileInputStream
import java.util.concurrent.ConcurrentHashMap

/**
 * Low memory killer вместо ядерного. Без него фоновые процессы гостя не умирают никогда: на Xiaomi 15
 * у S3 4.3 висело 35 app_process по 75–300 МБ, телефон уходил в своп, а `pm` отвечал по две минуты.
 *
 * Важность процессов (oom_adj, шкала −17…15) сообщает ActivityManager гостя:
 *  - до 4.3 — записью в /proc/<pid>/oom_adj; хост её не пускает (SELinux), прослойка гостя
 *    (native/guestshim) перенаправляет запись в FIFO /dev/aemu_oom строкой «\n<pid> a <adj>»;
 *  - 4.4+ — пакетами в сокет /dev/socket/lmkd (int big-endian: 1 = PROCPRIO pid uid adj, 2 = PROCREMOVE pid).
 * Когда памяти мало, убиваем самый неважный и самый толстый процесс, как это делал бы lowmemorykiller.
 */
/** [ramMb] > 0: guest RAM budget; background apps are killed when the guest's total RSS exceeds it. */
class GuestLmk(private val root: File, private val lowRam: Boolean, private val log: (String) -> Unit, private val ramMb: Int = 0,
               /** open32 only: the cpu cgroup sockets (dev/cpuctl). Elsewhere the file must stay absent: libcutils
                *  of a qemu-run firmware sees it, cannot open a socket and 6.0 zygote aborts at start (code 137) */
               private val cgroupSockets: Boolean = false) {
    private val adj = ConcurrentHashMap<Int, Int>()
    @Volatile private var running = false
    private var server: LocalServerSocket? = null
    private var bound: LocalSocket? = null
    private val fifo = File(root, "dev/aemu_oom")
    @Volatile var kills = 0
        private set

    fun start() {
        running = true
        runCatching {
            fifo.delete(); fifo.parentFile?.mkdirs()
            Os.mkfifo(fifo.absolutePath, "666".toInt(8))
            Os.chmod(fifo.absolutePath, "666".toInt(8))
            Thread({ readFifo() }, "lmk-fifo").apply { isDaemon = true; start() }
        }.onFailure { log("lmk: FIFO not created: $it") }
        runCatching { serveLmkd() }.onFailure { log("lmk: lmkd socket failed: $it") }
        if (cgroupSockets) for (bg in listOf(false, true)) runCatching { serveCgroup(bg) }.onFailure { log("lmk: cpuctl socket failed: $it") }
        else for (p in listOf("dev/cpuctl/tasks", "dev/cpuctl/bg_non_interactive/tasks")) runCatching {   // sockets left by older builds
            val f = File(root, p)
            if (android.system.OsConstants.S_ISSOCK(Os.lstat(f.path).st_mode)) f.delete()
        }
        Thread({ loop() }, "lmk").apply { isDaemon = true; start() }
    }

    fun stop() {
        running = false
        runCatching { server?.close() }; runCatching { bound?.close() }
        cgSocks.forEach { runCatching { it.close() } }; cgSocks.clear()
        // разбудить читателя FIFO
        runCatching { File(fifo.absolutePath).appendText("\n") }
        adj.clear()
    }

    private fun readFifo() {
        // O_RDWR: открытие не ждёт писателя и чтение не получает EOF, когда писатели уходят
        val fd = Os.open(fifo.absolutePath, OsConstants.O_RDWR or OsConstants.O_CLOEXEC, 0)
        val buf = ByteArray(4096)
        val line = StringBuilder()
        FileInputStream(fd).use { input ->
            while (running) {
                val n = try { input.read(buf) } catch (_: Exception) { break }
                if (n <= 0) break
                for (i in 0 until n) {
                    val c = buf[i].toInt().toChar()
                    if (c == '\n') { parseLine(line.toString()); line.setLength(0) } else if (line.length < 64) line.append(c)
                }
                // значение приходит отдельным write() без «\n»: строку закрывает «\n» следующей записи
            }
        }
    }

    private fun parseLine(s: String) {
        val p = s.trim().split(' ').filter { it.isNotEmpty() }
        if (p.size < 3) return
        val pid = p[0].toIntOrNull() ?: return
        val v = p[2].toIntOrNull() ?: return
        val a = if (p[1] == "s") scoreToAdj(v) else v
        adj[pid] = a
        renice(pid, a)
    }

    /**
     * 7.0+ sends oom_score_adj (−1000…999, cached from 900), 4.4–6.0 the old −17…15 scale (cached from 9).
     * Taking a raw 700 ("previous app") for a cached process killed services and visible apps every few seconds.
     */
    private fun lmkdAdj(v: Int): Int = when {
        v in -17..16 -> v
        v < 0 -> -17
        v >= 900 -> CACHED + (v - 900) / 12
        else -> CACHED - 1
    }

    private fun scoreToAdj(score: Int) = if (score >= 1000) 15 else score * 17 / 1000

    private fun serveLmkd() {
        val sock = File(root, "dev/socket/lmkd")
        sock.parentFile?.mkdirs(); sock.delete()
        val ls = LocalSocket(LocalSocket.SOCKET_SEQPACKET)
        ls.bind(LocalSocketAddress(sock.absolutePath, LocalSocketAddress.Namespace.FILESYSTEM))
        runCatching { Os.chmod(sock.absolutePath, "666".toInt(8)) }
        bound = ls
        val s = LocalServerSocket(ls.fileDescriptor)
        server = s
        Thread({
            while (running) {
                val c = try { s.accept() } catch (_: Exception) { break }
                Thread({ lmkdClient(c) }, "lmkd-client").apply { isDaemon = true; start() }
            }
        }, "lmkd-accept").apply { isDaemon = true; start() }
    }

    private fun lmkdClient(c: LocalSocket) {
        val buf = ByteArray(256)
        try {
            val input = c.inputStream
            while (running) {
                val n = input.read(buf)
                if (n <= 0) break
                val bb = java.nio.ByteBuffer.wrap(buf, 0, n)
                if (n < 8) continue
                when (bb.int) {
                    1 -> if (n >= 16) { val pid = bb.int; bb.int; val raw = bb.int; adj[pid] = lmkdAdj(raw); renice(pid, oldScale(raw)) }
                    2 -> { val pid = bb.int; adj.remove(pid); demoted.remove(pid) }
                }
            }
        } catch (_: Exception) {
        } finally { runCatching { c.close() } }
    }

    // ---------------------------------------------------------------- приоритеты

    /**
     * The phone's kernel puts background apps into a cgroup with a small CPU share; a guest's background apps had the
     * same priority as the one on screen, and the dozen apps a boot (or a broadcast) wakes took the screen down for
     * half a minute. Every thread of a background process (service, previous, cached) gets a low priority, and the
     * priorities the guest had set come back when the process returns to the foreground.
     */
    private val demoted = ConcurrentHashMap<Int, HashMap<Int, Int>>()

    /** the old -17..15 scale for both kinds of lmkd values: 7.0+ sends oom_score_adj (service 500, cached 900+) */
    private fun oldScale(v: Int): Int = when {
        v in -17..16 -> v
        v < 0 -> -17
        v >= 900 -> CACHED
        v >= 500 -> SERVICE
        else -> v / 100         // foreground 0, visible 100, perceptible 200, backup 300, heavy 400
    }

    /**
     * The firmware's own cpu cgroup moves (libcutils set_sched_policy, see cg_open in the kernel): each datagram is a
     * thread id. AMS puts there exactly what a phone keeps behind the screen, including the apps a boot's broadcasts
     * wake (their adj says "foreground" while a receiver runs, so the lmkd values alone miss them). Once these arrive
     * they decide, and the lmkd values no longer touch priorities.
     */
    private val cgSocks = java.util.Collections.synchronizedList(ArrayList<LocalSocket>())
    @Volatile private var cgroups = false
    private val bgSaved = ConcurrentHashMap<Int, Int>()

    private fun serveCgroup(bg: Boolean) {
        val f = File(root, if (bg) "dev/cpuctl/bg_non_interactive/tasks" else "dev/cpuctl/tasks")
        f.parentFile?.mkdirs(); f.delete()
        val ls = LocalSocket(LocalSocket.SOCKET_DGRAM)
        ls.bind(LocalSocketAddress(f.absolutePath, LocalSocketAddress.Namespace.FILESYSTEM))
        runCatching { Os.chmod(f.absolutePath, "666".toInt(8)) }
        cgSocks += ls
        Thread({
            val buf = ByteArray(64)
            val inp = ls.inputStream
            while (running) {
                val n = runCatching { inp.read(buf) }.getOrDefault(-1)
                if (n < 0) break
                val tid = String(buf, 0, n).trim().toIntOrNull() ?: continue
                cgroups = true
                runCatching {
                    if (bg) {
                        val cur = android.os.Process.getThreadPriority(tid)
                        bgSaved.putIfAbsent(tid, cur)
                        // 9, not lower: the guest's Process.setProcessGroup leaves a thread whose getpriority() is >= 10
                        // (ANDROID_PRIORITY_BACKGROUND) in the background, so the app would never come back to the front
                        if (cur < 9) android.os.Process.setThreadPriority(tid, 9)
                    } else bgSaved.remove(tid)?.let { android.os.Process.setThreadPriority(tid, it) }
                }
            }
        }, if (bg) "cpuctl-bg" else "cpuctl-fg").apply { isDaemon = true; start() }
    }

    private fun renice(pid: Int, a: Int) = runCatching {
        // (only firmwares whose cpu cgroup moves reach us, open32: elsewhere priorities stay as they always were)
        if (!cgroups || (a < CACHED && !demoted.containsKey(pid))) return@runCatching
        val want = when { a >= CACHED -> 19; a >= SERVICE -> 10; else -> null }
        val tids = File("/proc/$pid/task").list()?.mapNotNull { it.toIntOrNull() } ?: return@runCatching
        if (want != null) {
            val saved = demoted.getOrPut(pid) { HashMap() }
            for (t in tids) {
                if (t !in saved) saved[t] = runCatching { android.os.Process.getThreadPriority(t) }.getOrDefault(0)
                runCatching { android.os.Process.setThreadPriority(t, want) }
            }
        } else {
            val saved = demoted.remove(pid) ?: return@runCatching
            for (t in tids) runCatching { android.os.Process.setThreadPriority(t, saved[t] ?: 0) }
        }
    }

    // ---------------------------------------------------------------- убийца

    private fun memAvailableMb(): Long = runCatching {
        File("/proc/meminfo").useLines { l -> l.first { it.startsWith("MemAvailable:") } }
            .split(Regex("\\s+"))[1].toLong() / 1024
    }.getOrDefault(Long.MAX_VALUE)

    private fun rssMb(pid: Int): Long = runCatching {
        File("/proc/$pid/statm").readText().split(' ')[1].toLong() * 4 / 1024
    }.getOrDefault(-1)

    private fun loop() {
        // RSS процессов гостя не складываем: страницы зиготы общие и считались бы в каждом процессе.
        // Мерило — свободная память телефона; с lowRam держим запас больше и фона меньше.
        val minHost = if (lowRam) 2500L else MIN_HOST_MB
        val maxCached = if (ramMb > 0) (ramMb / 128).coerceIn(2, MAX_CACHED) else if (lowRam) 4 else MAX_CACHED
        while (running) {
            Thread.sleep(2000)
            adj.keys.removeAll { !File("/proc/$it").exists() }
            val cached = adj.filter { it.value >= CACHED }.keys
            if (cached.isEmpty()) continue
            val avail = memAvailableMb()
            // RSS overcounts shared zygote pages, so the budget check is approximate (upper bound)
            val overBudget = ramMb > 0 && adj.keys.sumOf { rssMb(it).coerceAtLeast(0) } > ramMb * 2
            // The count and the budget only matter when the phone runs short: with plenty free, killing a cached
            // service just makes the activity manager start it again (MIUI keeps 11-14 of them: a kill every 5 s,
            // providers gone under their clients, the CPU busy with restarts)
            val tight = avail < minHost * 2
            if (avail >= minHost && !(tight && (cached.size > maxCached || overBudget))) continue
            val sizes = cached.associateWith { rssMb(it) }
            // сначала самый неважный, среди равных — самый большой
            val victim = cached.maxWithOrNull(compareBy<Int> { adj[it] ?: 0 }.thenBy { sizes[it] ?: 0 }) ?: continue
            val a = adj[victim]
            runCatching { Os.kill(victim, OsConstants.SIGKILL) }
            adj.remove(victim)
            kills++
            log("memory: killed background process $victim (adj $a, ${sizes[victim]} MB; cached ${cached.size}, host free $avail MB)")
            Thread.sleep(3000)
        }
    }

    companion object {
        /** HIDDEN_APP_MIN_ADJ (≤4.3) = CACHED_APP_MIN_ADJ (4.4+) = 9 */
        const val CACHED = 9
        /** SERVICE_ADJ (old scale): started services and everything less important than them run at a low priority */
        const val SERVICE = 5
        const val MAX_CACHED = 10
        const val MIN_HOST_MB = 1500L
    }
}
