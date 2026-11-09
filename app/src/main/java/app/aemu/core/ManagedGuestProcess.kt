package app.aemu.core

import java.io.InputStream
import java.io.OutputStream
import java.util.concurrent.TimeUnit

/** A process whose identity is held by a pidfd until it has been reaped. */
internal class ManagedGuestProcess(
    private val delegate: Process,
    private val pid: Int,
    private var fd: Int,
    private val send: (Int, Int) -> Int,
    private val closeFd: (Int) -> Unit,
) : Process() {
    private val lock = Any()

    /**
     * Borrow the launcher-authenticated handle for a short, nonblocking operation.
     * The callback must not retain or close the fd, wait for the process, or perform
     * blocking socket I/O. Close and signal operations wait until the callback returns.
     * The consumer must check pidfd liveness; the process can exit during the callback.
     */
    internal fun <T> withAuthenticatedHandle(operation: (pid: Int, fd: Int) -> T): T = synchronized(lock) {
        check(fd >= 0 && delegate.isAlive) { "authenticated Guest process $pid is unavailable" }
        operation(pid, fd)
    }

    override fun getOutputStream(): OutputStream = delegate.outputStream
    override fun getInputStream(): InputStream = delegate.inputStream
    override fun getErrorStream(): InputStream = delegate.errorStream
    override fun isAlive(): Boolean = delegate.isAlive
    override fun exitValue(): Int = delegate.exitValue()

    private fun closeHandle() = synchronized(lock) {
        if (fd >= 0) { closeFd(fd); fd = -1 }
    }

    override fun waitFor(): Int = delegate.waitFor().also { closeHandle() }
    override fun waitFor(timeout: Long, unit: TimeUnit): Boolean =
        delegate.waitFor(timeout, unit).also { if (it) closeHandle() }

    private fun terminate(signal: Int) = synchronized(lock) {
        if (fd < 0) {
            check(!delegate.isAlive) { "Guest process $pid has no pidfd" }
            return@synchronized
        }
        val result = send(fd, signal)
        if (result != 0 && !(result == -3 && !delegate.isAlive)) {
            error("Guest process $pid signal $signal failed: errno=${-result}")
        }
    }

    override fun destroy() = terminate(15)
    override fun destroyForcibly(): Process { terminate(9); return this }
}
