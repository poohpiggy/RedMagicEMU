package app.aemu.core

import java.io.File
import java.io.RandomAccessFile

/**
 * Size limits for the image's logs. Without them a looping service filled run/zygote.log to 455 MB and
 * root/dev/log/main to 247 MB. Also keeps run/crashes.log: every non-zero service exit across boots,
 * so crash statistics survive aemu.log being cleared at each boot.
 */
object LogCap {
    /** one service's output per run */
    const val SERVICE_RUN = 16L shl 20
    /** a log file at boot: above this only the tail is kept */
    private const val AT_BOOT = 8L shl 20
    private const val TAIL = 1L shl 20
    /** a guest log buffer or the direct zygote log while running: above this it is emptied */
    private const val RUNNING = 32L shl 20
    private const val CRASHES = 256L shl 10

    private fun guestLogs(paths: VmPaths) = listOf("main", "system", "radio").map { File(paths.root, "dev/log/$it") }

    fun trimAtBoot(paths: VmPaths) {
        val files = (paths.bin.listFiles { f -> f.name.endsWith(".log") || f.name.endsWith(".trace") }?.toList() ?: emptyList()) + guestLogs(paths)
        for (f in files) runCatching { if (f.isFile && f.length() > AT_BOOT) keepTail(f) }
    }

    private fun keepTail(f: File) {
        val b = ByteArray(TAIL.toInt())
        RandomAccessFile(f, "rw").use { r ->
            r.seek(r.length() - TAIL); r.readFully(b)
            r.seek(0); r.write(b); r.setLength(TAIL)
        }
    }

    /** empties guest log buffers and service logs that outgrow the limit while the guest runs */
    fun watch(paths: VmPaths, done: () -> Boolean) {
        Thread({
            while (!done()) {
                runCatching { Thread.sleep(20_000) }
                if (done()) break
                val files = guestLogs(paths) + (paths.bin.listFiles { f -> f.name.endsWith(".log") }?.toList() ?: emptyList())
                for (f in files) runCatching { if (f.isFile && f.length() > RUNNING) RandomAccessFile(f, "rw").use { it.setLength(0) } }
            }
        }, "aemu-logcap").apply { isDaemon = true; start() }
    }

    /** one line per crash: "date time service code [reason]" */
    fun crash(paths: VmPaths, name: String, code: Int, reason: String? = null) = runCatching {
        val f = File(paths.bin, "crashes.log")
        if (f.length() > CRASHES) keepTail(f, CRASHES / 2)
        val t = java.text.SimpleDateFormat("yyyy-MM-dd HH:mm:ss", java.util.Locale.US).format(java.util.Date())
        f.appendText("$t $name $code${reason?.let { " | " + it.replace('\n', ' ') } ?: ""}\n")
    }

    private fun keepTail(f: File, n: Long) {
        val b = ByteArray(n.toInt())
        RandomAccessFile(f, "rw").use { r -> r.seek(r.length() - n); r.readFully(b); r.seek(0); r.write(b); r.setLength(n) }
    }
}
