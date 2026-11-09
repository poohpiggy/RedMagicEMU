package app.aemu.core

/** A receiver authenticates its transport; Guest authorization is separate. */
internal fun interface HostVibrationInput {
    fun read(sequence: Long): Int
}

/**
 * A nonblocking receiver feeds exactly one session. The caller must supply an
 * authenticated receiver and owns its socket; this class does not authorize
 * a Guest application or provide an entry point. Guest authorization must be
 * established before dispatch. No physical motor is created here. After any
 * malformed result or failure, the session is retired.
 */
internal class HostVibrationPump(
    private val control: HostVibrationControl,
    private val receive: HostVibrationInput,
) : AutoCloseable {
    private val session = control.start()
    private var sequence = 1L
    private var closed = false
    private var closeFailure: Throwable? = null

    fun pollOnce(): Boolean = pollOnce { action -> action(); true }

    /** A session can drop an in-flight read after it has begun closing. */
    @Synchronized fun pollOnce(dispatch: ((() -> Unit) -> Boolean)): Boolean {
        check(!closed) { "Vibration session closed" }
        try {
            check(sequence < Long.MAX_VALUE) { "Vibration sequence exhausted" }
            val command = receive.read(sequence)
            if (command == 0) return false
            check(command in 1..(HostVibrationControl.MAX_PULSE_MS.toInt() + 1)) {
                "Vibration transport rejected or returned invalid command"
            }
            val dispatched = dispatch {
                val accepted = if (command == 1) control.cancel(session)
                               else control.vibrate(session, (command - 1).toLong())
                check(accepted) { "Vibration session no longer active" }
            }
            if (!dispatched) return false
            sequence++
            return true
        } catch (failure: Throwable) {
            try { close() } catch (cleanupFailure: Throwable) {
                if (cleanupFailure !== failure) failure.addSuppressed(cleanupFailure)
            }
            throw failure
        }
    }

    @Synchronized override fun close() {
        if (closed) {
            closeFailure?.let { throw it }
            return
        }
        closed = true
        try {
            check(control.stop(session)) { "Vibration session no longer active" }
        } catch (failure: Throwable) {
            closeFailure = failure
            throw failure
        }
    }
}
