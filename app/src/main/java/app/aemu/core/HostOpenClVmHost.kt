package app.aemu.core

import java.io.File

/** Owns the native OpenCL listener for the current Guest boot. */
internal class HostOpenClVmHost(private val log: (String) -> Unit) {
    private val lock = Any()
    private var handle = 0L
    private var cancelled = false

    fun begin() = synchronized(lock) {
        check(handle == 0L)
        cancelled = false
    }

    fun start(root: File, runner: File) {
        val failure = try {
            val session = synchronized(lock) {
                if (cancelled) return
                check(handle == 0L)
                System.loadLibrary("aemuclhost")
                nativeStart(root.absolutePath, runner.absolutePath).also { handle = it }
            }
            nativeAwaitReady(session, 5_000)
        } catch (error: LinkageError) {
            error.message ?: error.javaClass.simpleName
        } catch (error: Exception) {
            error.message ?: error.javaClass.simpleName
        }
        if (synchronized(lock) { cancelled }) return
        if (failure == null) {
            log("Guest native OpenCL host ready")
        } else {
            cancel()
            log("Guest native OpenCL unavailable: $failure")
        }
    }

    fun cancel() = synchronized(lock) {
        cancelled = true
        if (handle != 0L) nativeCancel(handle)
    }

    /** VmLifecycle serializes cleanup. A timed-out worker remains owned by this VM for retry. */
    fun stop() {
        val session = synchronized(lock) { handle }
        if (session == 0L) return
        val failure = nativeAwaitStopped(session, 5_000)
        check(failure == null) { failure.orEmpty() }
        synchronized(lock) {
            nativeRelease(session)
            handle = 0L
        }
    }

    private external fun nativeStart(root: String, runner: String): Long
    private external fun nativeAwaitReady(handle: Long, timeoutMs: Int): String?
    private external fun nativeCancel(handle: Long)
    private external fun nativeAwaitStopped(handle: Long, timeoutMs: Int): String?
    private external fun nativeRelease(handle: Long)
}
