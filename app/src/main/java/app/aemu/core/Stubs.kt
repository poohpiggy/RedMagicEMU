package app.aemu.core

import android.net.LocalSocket
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.OutputStream
import java.util.Collections

/**
 * Заглушка vold: MountService видит одну «вставленную» карту по пути из прошивки.
 * Сама карта — каталог на хосте, qemu подменяет гостевой путь (DHD_SDCARD).
 */
class VoldStub(
    paths: VmPaths,
    private val mountPoint: String,
    private val log: (String) -> Unit,
    sockName: String = "vold",
    /** остальные тома прошивки (съёмные карты, USB): сообщаем о них как о «нет носителя» */
    private val others: List<String> = emptyList(),
    private val api: Int = 19,
    /** vold 1.0 (1.5-1.6) publishes the card's state itself: EXTERNAL_STORAGE_STATE, read by Environment */
    private val setProp: ((String, String) -> Unit)? = null,
) {
    // 2.0-3.2: the card starts Idle, as from the real vold at boot. MountService.onDaemonConnected then marks it
    // unmounted and mounts it itself on BOOT_COMPLETED; the 605 event that follows is what broadcasts MEDIA_MOUNTED,
    // and only that broadcast makes the media scanner attach the "external" volume of MediaProvider. A card that is
    // already Mounted is logged as "Media already mounted on daemon connection" with no broadcast, and the camera's
    // insert into content://media/external fails with "Unknown URI".
    @Volatile var state = if (api in 5..13) IDLE else MOUNTED
    private val label = mountPoint.substringAfterLast('/').ifEmpty { "sdcard" }
    private val clients = Collections.synchronizedList(ArrayList<LocalSocket>())

    private val server = UnixServer(paths.socket(sockName), sockName) { c -> serveOne(c) }

    fun serve() = server.start(log)
    fun stop() { server.stop(); clients.clear() }

    private fun serveOne(c: LocalSocket) {
        clients.add(c)
        try {
            val out = c.outputStream
            val ins = c.inputStream
            val buf = ByteArray(4096)
            val acc = StringBuilder()
            while (true) {
                val n = try { ins.read(buf) } catch (e: Exception) { -1 }
                if (n <= 0) break
                for (i in 0 until n) {
                    val b = buf[i].toInt() and 0xff
                    if (b == 0) {
                        val cmd = acc.toString(); acc.setLength(0)
                        if (cmd.isNotEmpty()) answer(cmd, out)
                    } else acc.append(b.toChar())
                }
            }
        } finally {
            clients.remove(c)
        }
    }

    private fun answer(cmd: String, out: OutputStream) {
        // 1.5-1.6: vold 1.0 speaks plain words, no codes (MountListener of services.jar: send_ums_status,
        // mount_volume:<path>, eject_media:<path>, format_media:<path>; events ums_*, volume_mounted:<path>, ...)
        if (api in 3..4) { answerV1(cmd.trim(), out); return }
        val parts = cmd.trim().split(Regex("\\s+"))
        val seq = parts.getOrNull(0)?.toIntOrNull()
        val args = if (seq != null) parts.drop(1) else parts
        val head = args.getOrElse(0) { "" }
        val sub = args.getOrElse(1) { "" }
        var after: (() -> Unit)? = null
        val reply: List<String> = when {
            // 6.0 vold: volumes are announced as events. One emulated volume over the shared folder; MountService
            // asks to mount it and gets its paths (/storage/emulated/<user>, kept in /data/media).
            api >= 23 && head == "volume" && sub == "reset" -> {
                after = { event("650 emulated 2 \"\" \"\"", "651 emulated 0") }
                listOf("200 Command succeeded")
            }
            api >= 23 && head == "volume" && sub == "mount" -> {
                val id = args.getOrElse(2) { "emulated" }
                after = { event("651 $id 1", "656 $id /data/media", "655 $id /storage/emulated", "651 $id 2") }
                listOf("200 Command succeeded")
            }
            api >= 23 && head == "volume" && sub == "unmount" -> {
                val id = args.getOrElse(2) { "emulated" }
                after = { event("651 $id 5", "651 $id 0") }
                listOf("200 Command succeeded")
            }
            // с томами из storage_list (4.x) сообщаем только о них: основное хранилище там эмулируемое,
            // а незнакомый путь MountService Samsung считает ошибкой и обрывает список
            head == "volume" && sub == "list" -> (if (others.isEmpty()) listOf("110 $label $mountPoint $state")
                else others.mapIndexed { i, v -> "110 ${v.substringAfterLast('/')} $v ${if (i == 0 && v == mountPoint) state else 0}" }) +
                "200 Volumes listed."
            head == "volume" && sub == "mount" -> { state = MOUNTED; after = { announce(IDLE, MOUNTED) }; listOf("200 Volume mounted.") }
            head == "volume" && sub == "unmount" -> { state = IDLE; after = { announce(MOUNTED, IDLE) }; listOf("200 Volume unmounted.") }
            head == "volume" && sub == "format" -> listOf("200 Volume formatted.")
            head == "share" && sub == "status" -> listOf("210 ${args.getOrElse(2) { "ums" }} unavailable")
            head == "share" || head == "unshare" -> listOf("200 Share operation succeeded.")
            head == "storage" && sub == "users" -> listOf("200 Storage user list.")
            head == "asec" && sub == "list" -> listOf("200 Asec listed.")
            head == "asec" && sub == "path" -> listOf("211 /mnt/asec/${args.getOrElse(2) { "" }}")
            head == "obb" && sub == "list" -> listOf("200 Obb listed.")
            // the answer is one number that MountService parses whole (Samsung's moveDumpstates dies in system_server
            // on anything else); cryptocomplete: 1 = not encrypted
            head == "cryptfs" -> listOf(if (sub == "cryptocomplete") "200 1" else "200 0")
            else -> listOf("200 ok")
        }
        send(out, reply.map { withSeq(it, seq) })
        after?.invoke()
    }

    private fun answerV1(cmd: String, out: OutputStream) {
        val msgs: List<String> = when {
            // asked once when MountListener connects: USB storage is off and unplugged, then the card's state
            cmd == "send_ums_status" -> listOf("ums_disabled", "ums_disconnected",
                if (state == MOUNTED) "volume_mounted:$mountPoint" else "volume_unmounted:$mountPoint")
            cmd.startsWith("mount_volume:") -> { state = MOUNTED; listOf("volume_mounted:$mountPoint") }
            cmd.startsWith("eject_media:") -> { state = IDLE; listOf("volume_ejecting:$mountPoint", "volume_unmounted:$mountPoint") }
            cmd.startsWith("format_media:") -> listOf("volume_unmounted:$mountPoint")
            cmd == "enable_ums" || cmd == "disable_ums" -> listOf("ums_disabled")
            else -> emptyList()
        }
        // the property first: the framework reacts to the event by reading it (strings as vold 1.0's volmgr)
        msgs.lastOrNull { it.startsWith("volume_") }?.let { e ->
            setProp?.invoke("EXTERNAL_STORAGE_STATE", if (e.startsWith("volume_mounted:")) "mounted" else "unmounted")
        }
        if (msgs.isNotEmpty()) send(out, msgs)
    }

    private fun withSeq(line: String, seq: Int?): String {
        if (seq == null) return line
        val sp = line.indexOf(' ')
        if (sp <= 0) return line
        val code = line.substring(0, sp).toIntOrNull() ?: return line
        if (code in 600..699) return line
        return "$code $seq${line.substring(sp)}"
    }

    private fun event(vararg msgs: String) {
        val list = synchronized(clients) { ArrayList(clients) }
        for (c in list) runCatching { send(c.outputStream, msgs.toList()) }
    }

    fun announce(from: Int, to: Int) {
        if (api >= 23) return
        if (api in 3..4) {
            setProp?.invoke("EXTERNAL_STORAGE_STATE", if (to == MOUNTED) "mounted" else "unmounted")
            event(if (to == MOUNTED) "volume_mounted:$mountPoint" else "volume_unmounted:$mountPoint")
            return
        }
        val msg = "605 Volume $label $mountPoint state changed from $from (${name(from)}) to $to (${name(to)})"
        val list = synchronized(clients) { ArrayList(clients) }
        for (c in list) runCatching { send(c.outputStream, listOf(msg)) }
    }

    fun rescan() {
        state = MOUNTED
        announce(IDLE, MOUNTED)
    }

    private fun name(s: Int) = when (s) {
        0 -> "No-Media"; 1 -> "Idle-Unmounted"; 2 -> "Pending"; 3 -> "Checking"; 4 -> "Mounted"
        5 -> "Unmounting"; 6 -> "Formatting"; 7 -> "Shared-Unmounted"; else -> "Unknown"
    }

    @Synchronized
    private fun send(out: OutputStream, msgs: List<String>) {
        val b = ByteArrayOutputStream()
        for (s in msgs) { b.write(s.toByteArray()); b.write(0) }
        out.write(b.toByteArray())
        out.flush()
    }

    companion object {
        const val IDLE = 1
        const val MOUNTED = 4
    }
}

/** «Звонок кадра»: GL-сервер сообщает о новом кадре — будим отрисовку. */
class FrameBell(paths: VmPaths, private val log: (String) -> Unit, private val onFrame: () -> Unit) {
    @Volatile var rings = 0L
        private set
    private val server = UnixServer(paths.frameSock, "frame") { c ->
        val ins = c.inputStream
        val b = ByteArray(64)
        while (true) {
            val n = try { ins.read(b) } catch (e: Exception) { -1 }
            if (n <= 0) break
            rings += n
            onFrame()
        }
    }
    fun serve() = server.start(log)
    fun stop() = server.stop()
}

/**
 * /dev/log/events — канал (FIFO), который хост держит открытым и вычитывает.
 * Обычным файлом его делать нельзя: часть служб (MIUI Whetstone и др.) читает журнал событий как
 * устройство logger — на обычном файле чтение сразу упирается в конец, и служба крутит цикл
 * «read error», забивая процессор и журнал (миллионы строк за минуту). Из канала читатель ждёт данных.
 * Сами события эмулятору не нужны — выбрасываем.
 */
class EventsSink(paths: VmPaths, private val log: (String) -> Unit) {
    private val fifo = File(paths.root, "dev/log/events")
    @Volatile private var raf: java.io.RandomAccessFile? = null

    fun start() {
        runCatching {
            val mode = runCatching { android.system.Os.stat(fifo.absolutePath).st_mode and android.system.OsConstants.S_IFMT }.getOrDefault(0)
            if (mode != android.system.OsConstants.S_IFIFO) {
                fifo.parentFile?.mkdirs(); fifo.delete()
                android.system.Os.mkfifo(fifo.absolutePath, "666".toInt(8))
            }
            // O_RDWR: открытие не ждёт второй стороны, а писатели гостя никогда не упираются в «нет читателя»
            val r = java.io.RandomAccessFile(fifo, "rw").also { raf = it }
            Thread({
                val buf = ByteArray(1 shl 16)
                while (true) { if (runCatching { r.read(buf) }.getOrDefault(-1) < 0) break }
            }, "aemu-events").apply { isDaemon = true; start() }
        }.onFailure { log("event log: channel failed: ${it.message}") }
    }

    fun stop() { runCatching { raf?.close() }; raf = null }
}

/**
 * logd (Android 5+): liblog sends every record as a datagram to /dev/socket/logdw instead of writing the /dev/log files.
 * Without a receiver all guest logging is lost. The host binds that socket and appends each record to
 * dev/log/<buffer> in the same "[prio][tag]\0[msg]\0" form older guests write, so GuestLog reads both alike.
 * Datagram: log_id:u8, tid:u16, realtime:8 bytes, then the payload (prio, tag\0, msg\0); events are dropped.
 */
class LogdSink(paths: VmPaths, private val log: (String) -> Unit) {
    private val sock = File(paths.root, "dev/socket/logdw")
    private val dir = File(paths.root, "dev/log")
    @Volatile private var s: android.net.LocalSocket? = null

    fun start() {
        runCatching {
            sock.parentFile?.mkdirs(); sock.delete()
            val ls = android.net.LocalSocket(android.net.LocalSocket.SOCKET_DGRAM)
            ls.bind(android.net.LocalSocketAddress(sock.absolutePath, android.net.LocalSocketAddress.Namespace.FILESYSTEM))
            sock.setWritable(true, false)
            s = ls
            val outs = HashMap<Int, java.io.FileOutputStream>()
            Thread({
                val buf = ByteArray(5 * 1024 + 64)
                val inp = ls.inputStream
                while (true) {
                    val n = runCatching { inp.read(buf) }.getOrDefault(-1)
                    if (n < 0) break
                    if (n <= 12) continue
                    val id = buf[0].toInt() and 0xff
                    val name = when (id) { 0 -> "main"; 1 -> "radio"; 3 -> "system"; 4 -> "main"; else -> continue }
                    val o = outs.getOrPut(id) { java.io.FileOutputStream(File(dir, name), true) }
                    runCatching { o.write(buf, 11, n - 11); if (buf[n - 1].toInt() != 0) o.write(0) }
                }
                outs.values.forEach { runCatching { it.close() } }
            }, "aemu-logd").apply { isDaemon = true; start() }
        }.onFailure { log("logd: socket failed: ${it.message}") }
    }

    fun stop() { runCatching { s?.close() }; s = null; sock.delete() }
}
