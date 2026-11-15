package app.aemu.core

import android.net.LocalServerSocket
import android.net.LocalSocket
import android.net.LocalSocketAddress
import android.system.Os
import android.system.OsConstants
import java.io.File
import java.io.FileDescriptor

/**
 * Unix-сокет в файловой системе, который слушает приложение. Гость (через qemu) подключается
 * к нему по гостевому пути — qemu переписывает путь в дерево прошивки.
 * Все дескрипторы помечаются CLOEXEC, иначе они утекают в процессы гостя.
 */
class UnixServer(
    val file: File,
    private val name: String,
    private val maxClients: Int = 0,
    private val onClient: (LocalSocket) -> Unit,
) {
    private val admission = if (maxClients > 0) DnsClientAdmission(maxClients) { error ->
        android.util.Log.w("UnixServer", "$name: client close failed", error)
    } else null
    @Volatile private var server: LocalServerSocket? = null
    @Volatile private var bound: LocalSocket? = null
    @Volatile var clients = 0
        private set

    val running: Boolean get() = server != null && file.exists()

    @Synchronized
    fun start(log: (String) -> Unit): Boolean {
        if (running) return true
        stop()
        file.parentFile?.mkdirs()
        file.delete()
        return try {
            val ls = LocalSocket(LocalSocket.SOCKET_STREAM)
            ls.bind(LocalSocketAddress(file.absolutePath, LocalSocketAddress.Namespace.FILESYSTEM))
            cloexec(ls.fileDescriptor)
            runCatching { Os.chmod(file.absolutePath, "666".toInt(8)) }
            bound = ls
            val s = LocalServerSocket(ls.fileDescriptor)
            server = s
            val generation = admission?.start()
            Thread({
                while (true) {
                    val c = try { s.accept() } catch (e: Exception) { break }
                    cloexec(c.fileDescriptor)
                    val lease = admission?.tryAdmit(java.io.Closeable { c.close() }, generation!!)
                    if (admission != null && lease == null) continue
                    clients++
                    try {
                        Thread({
                            try { onClient(c) } catch (_: Throwable) {
                            } finally {
                                clients--
                                if (lease != null) lease.close() else runCatching { c.close() }
                            }
                        }, "$name-client").apply { isDaemon = true; start() }
                    } catch (failure: Throwable) {
                        clients--
                        if (lease != null) lease.close() else runCatching { c.close() }
                        throw failure
                    }
                }
            }, "$name-accept").apply { isDaemon = true; start() }
            true
        } catch (e: Throwable) {
            admission?.stop()
            log("$name: socket failed: $e")
            false
        }
    }

    @Synchronized
    fun stop() {
        runCatching { server?.close() }
        runCatching { bound?.close() }
        server = null
        bound = null
        admission?.stop()
        clients = 0
    }

    companion object {
        fun cloexec(fd: FileDescriptor?) {
            if (fd == null) return
            runCatching { Os.fcntlInt(fd, OsConstants.F_SETFD, OsConstants.FD_CLOEXEC) }
        }
    }
}
