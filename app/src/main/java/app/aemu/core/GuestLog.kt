package app.aemu.core

import java.io.File
import java.io.RandomAccessFile

/**
 * Журнал гостя. Гостевой liblog пишет в /dev/log/main записи вида
 *   [приоритет:1 байт][тег]\0[сообщение]\0
 * (заголовок добавило бы ядро, но драйвера logger нет — файл обычный).
 */
object GuestLog {
    data class Rec(val prio: Char, val tag: String, val msg: String)

    fun tail(root: File, bytes: Int = 256 * 1024, buffer: String = "main"): List<Rec> {
        val f = File(root, "dev/log/$buffer")
        if (!f.isFile || f.length() == 0L) return emptyList()
        val data = runCatching {
            RandomAccessFile(f, "r").use { r ->
                val n = minOf(r.length(), bytes.toLong()).toInt()
                r.seek(r.length() - n)
                ByteArray(n).also { r.readFully(it) }
            }
        }.getOrNull() ?: return emptyList()
        return parse(data)
    }

    fun parse(data: ByteArray): List<Rec> {
        val out = ArrayList<Rec>()
        var i = 0
        while (i < data.size - 2) {
            val p = data[i].toInt() and 0xff
            val end = indexOf(data, i + 1)
            if (end < 0) break
            if (p in 2..8 && end - i - 1 in 1..48) {
                val tag = String(data, i + 1, end - i - 1, Charsets.UTF_8)
                if (tag.none { it < ' ' }) {
                    val j = end + 1
                    var end2 = indexOf(data, j)
                    if (end2 < 0) end2 = data.size
                    out.add(Rec("??VDIWEFS"[p], tag, String(data, j, end2 - j, Charsets.UTF_8)))
                    i = end2 + 1
                    continue
                }
            }
            i++
        }
        return out
    }

    fun contains(root: File, vararg needles: String): Boolean {
        val f = File(root, "dev/log/main")
        if (!f.isFile) return false
        val t = runCatching { String(f.readBytes(), Charsets.ISO_8859_1) }.getOrDefault("")
        return needles.any { t.contains(it) }
    }

    fun clear(root: File) {
        for (n in listOf("main", "system", "radio", "events")) {
            val f = File(root, "dev/log/$n")
            if (f.isFile) runCatching { RandomAccessFile(f, "rw").use { it.setLength(0) } }
        }
    }

    /**
     * zygote kills itself (SIGKILL, exit 137) when system_server dies. Extract the most recent
     * system_server crash marker across main/system/crash guest log buffers, if any.
     */
    fun systemServerDeath(root: File): String? = runCatching {
        val markers = listOf("FATAL EXCEPTION IN SYSTEM PROCESS", "WATCHDOG KILLING SYSTEM PROCESS", ">>> system_server <<<")
        var best: Pair<Long, String>? = null
        for (buf in listOf("main", "system", "crash")) {
            val f = File(root, "dev/log/$buf")
            if (!f.isFile) continue
            val len = f.length(); val n = minOf(len, 2_000_000L).toInt()
            if (n <= 0) continue
            val b = ByteArray(n)
            RandomAccessFile(f, "r").use { r -> r.seek(len - n); r.readFully(b) }
            val t = String(b, Charsets.ISO_8859_1)
            for (m in markers) {
                val i = t.lastIndexOf(m); if (i < 0) continue
                val tail = t.substring(i, minOf(t.length, i + 600)).map { if (it in ' '..'~') it else ' ' }.joinToString("")
                    .replace(Regex(" {2,}"), " ").take(240)
                if (best == null || f.lastModified() > best!!.first) best = f.lastModified() to tail
            }
        }
        best?.second
    }.getOrNull()

    /**
     * Export non-root diagnostic logs: crashes.log plus crash, system, and main guest logcat buffers
     * so BootAnimation/RenderEngine spam in main does not hide system_server failures.
     */
    fun exportDiagnosticLog(root: File, bin: File): List<String> {
        val out = ArrayList<String>()
        val crashes = File(bin, "crashes.log")
        if (crashes.isFile && crashes.length() > 0L) {
            out.add("=== crashes.log ===")
            runCatching {
                val n = minOf(crashes.length(), 32L * 1024).toInt()
                val bytes = RandomAccessFile(crashes, "r").use { r ->
                    r.seek(r.length() - n)
                    ByteArray(n).also { r.readFully(it) }
                }
                String(bytes, Charsets.UTF_8).lineSequence().filter { it.isNotEmpty() }.forEach { out.add(it) }
            }
        }
        for ((buffer, bytesLimit, lineLimit) in listOf(
            Triple("crash", 128 * 1024, 300),
            Triple("system", 256 * 1024, 400),
            Triple("main", 256 * 1024, 400),
        )) {
            val recs = tail(root, bytesLimit, buffer).takeLast(lineLimit)
            if (recs.isEmpty()) continue
            out.add("=== dev/log/$buffer ===")
            recs.forEach { out.add("${it.prio} ${it.tag}: ${it.msg}") }
        }
        return out
    }

    private fun indexOf(a: ByteArray, from: Int): Int {
        for (k in from until a.size) if (a[k] == 0.toByte()) return k
        return -1
    }
}

internal fun formatZygote137Detail(death: String?): String =
    if (!death.isNullOrBlank()) "system_server died and took zygote with it: $death"
    else "killed with no system_server crash in guest logs (likely host OOM or Android child-process limit — disable child process restrictions in host Developer options)"
