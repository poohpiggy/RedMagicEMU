package app.aemu.core

/** A preauthenticated, owned transport. This interface does not authorize Guest callers. */
internal interface HostVibrationBinderTransport : AutoCloseable {
    /** Blocks until an event or a clean stop. requestStop must wake this call. */
    fun next(): HostVibrationBinderEvent?
    /** Must complete promptly; false must not send a success reply. */
    fun complete(token: Long, success: Boolean)
    fun requestStop()
}

internal data class HostVibrationBinderEvent(val token: Long, val code: Int, val durationMs: Long)

/** Offline coordinator only; an authenticated native transport is not yet wired to the VM. */
internal class HostVibrationBinderBridge(
    private val control: HostVibrationControl,
    private val transport: HostVibrationBinderTransport,
) : AutoCloseable {
    private val lock = Object()
    private val session = try { control.start() } catch (failure: Throwable) {
        try { transport.close() } catch (cleanup: Throwable) { if (cleanup !== failure) failure.addSuppressed(cleanup) }
        throw failure
    }
    private var closing = false
    private var retired = false
    private var finished = false
    private var terminalFailure: Throwable? = null
    private val worker = Thread({ run() }, "vibration-binder-bridge").apply { isDaemon = true }

    init {
        try { worker.start() } catch (failure: Throwable) {
            try { control.stop(session) } catch (cleanup: Throwable) { if (cleanup !== failure) failure.addSuppressed(cleanup) }
            try { transport.close() } catch (cleanup: Throwable) { if (cleanup !== failure) failure.addSuppressed(cleanup) }
            throw failure
        }
    }

    private fun record(error: Throwable) {
        val previous = terminalFailure
        if (previous == null) terminalFailure = error
        else if (previous !== error && previous.suppressed.none { it === error }) previous.addSuppressed(error)
    }

    /** Caller holds lock; marking retired first preserves the result if cancellation fails. */
    private fun retire() {
        if (!retired) {
            retired = true
            try { check(control.stop(session)) } catch (error: Throwable) { record(error) }
        }
    }

    private fun run() {
        try {
            while (true) {
                val event = transport.next() ?: break
                val keepGoing = synchronized(lock) {
                    if (closing) false else {
                        try {
                            check(event.token > 0) { "Invalid vibration token" }
                            when (event.code) {
                                1 -> check(event.durationMs == 0L) { "Invalid capabilities request" }
                                2 -> { check(event.durationMs == 0L); check(control.cancel(session)) }
                                3 -> check(control.vibrate(session, event.durationMs)) { "Invalid vibration duration" }
                                else -> error("Unsupported vibration request")
                            }
                        } catch (failure: Throwable) {
                            try { transport.complete(event.token, false) }
                            catch (cleanup: Throwable) { if (cleanup !== failure) failure.addSuppressed(cleanup) }
                            throw failure
                        }
                        transport.complete(event.token, true)
                        true
                    }
                }
                if (!keepGoing) break
            }
        } catch (error: Throwable) { synchronized(lock) { record(error) } }
        finally {
            synchronized(lock) { closing = true; retire() }
            try { transport.close() } catch (error: Throwable) { synchronized(lock) { record(error) } }
            synchronized(lock) { finished = true; lock.notifyAll() }
        }
    }

    /** Wakes the worker, waits for cleanup, and reports any failure; never called by the worker. */
    override fun close() {
        check(Thread.currentThread() !== worker) { "Cannot close from the vibration worker" }
        val signal = synchronized(lock) {
            if (closing) false else { closing = true; retire(); true }
        }
        if (signal) try { transport.requestStop() } catch (error: Throwable) {
            synchronized(lock) { record(error) }
            worker.interrupt()
        }
        synchronized(lock) {
            var interrupted: InterruptedException? = null
            while (!finished) try { lock.wait() } catch (error: InterruptedException) {
                if (interrupted == null) interrupted = error else interrupted.addSuppressed(error)
            }
            interrupted?.let { error ->
                Thread.currentThread().interrupt()
                terminalFailure?.let { it.addSuppressed(error); throw it }
                throw error
            }
            terminalFailure?.let { throw it }
        }
    }
}
