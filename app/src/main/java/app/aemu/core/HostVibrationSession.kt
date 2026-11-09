package app.aemu.core

/**
 * Owns a nonblocking receiver until VM stop, transport failure, or process exit.
 * Receiver authentication is independent of Guest application authorization;
 * this session must not be started in the product until both are established.
 * Background failures are reported on close; the future owner must supervise
 * and close the session. A receiver must not perform blocking reads.
 */
internal class HostVibrationSession(
    control: HostVibrationControl,
    private val input: HostVibrationInput,
) : AutoCloseable {
    private val inputResource = requireNotNull(input as? AutoCloseable) { "Input must be owned and closeable" }
    private val pump = try { HostVibrationPump(control, input) } catch (failure: Throwable) {
        try { inputResource.close() } catch (cleanup: Throwable) { if (cleanup !== failure) failure.addSuppressed(cleanup) }
        throw failure
    }
    private val lock = Object()
    private var closing = false
    private var finished = false
    private var terminalFailure: Throwable? = null
    private val worker = Thread({ run() }, "vibration-session").apply { isDaemon = true }

    init {
        try { worker.start() }
        catch (failure: Throwable) {
            try { pump.close() } catch (cleanup: Throwable) { if (cleanup !== failure) failure.addSuppressed(cleanup) }
            try { inputResource.close() } catch (cleanup: Throwable) { if (cleanup !== failure) failure.addSuppressed(cleanup) }
            throw failure
        }
    }

    private fun run() {
        var failure: Throwable? = null
        try {
            while (synchronized(lock) { !closing }) {
                val dispatched = pump.pollOnce { action ->
                    synchronized(lock) {
                        if (closing) false else { action(); true }
                    }
                }
                if (!dispatched) synchronized(lock) { if (!closing) lock.wait(20) }
            }
        } catch (error: Throwable) { failure = error }
        finally {
            try { pump.close() } catch (error: Throwable) {
                if (failure == null) failure = error
                else if (failure !== error && failure.suppressed.none { it === error }) failure.addSuppressed(error)
            }
            try { inputResource.close() } catch (error: Throwable) {
                if (failure == null) failure = error
                else if (failure !== error && failure.suppressed.none { it === error }) failure.addSuppressed(error)
            }
            synchronized(lock) { terminalFailure = failure; finished = true; lock.notifyAll() }
        }
    }

    /** Blocks until the receiver has stopped and both cleanup actions were attempted. */
    override fun close() {
        check(Thread.currentThread() !== worker) { "Cannot close the session from its receiver" }
        synchronized(lock) {
            closing = true
            lock.notifyAll()
            var interruption: InterruptedException? = null
            while (!finished) try { lock.wait() } catch (error: InterruptedException) {
                if (interruption == null) interruption = error else interruption.addSuppressed(error)
            }
            val failure = terminalFailure
            interruption?.let { error ->
                Thread.currentThread().interrupt()
                if (failure != null) { failure.addSuppressed(error); throw failure }
                throw error
            }
            failure?.let { throw it }
        }
    }
}
