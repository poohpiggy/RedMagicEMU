package app.aemu.core

/**
 * Serializes a single VM session's bounded host vibration requests and stop.
 *
 * This is not connected to a bridge and does not authenticate callers or
 * enforce Guest permissions. Its owner must validate every request first.
 * Sink callbacks must be short and synchronous; work queued by a sink is not
 * protected by this controller. Use a new controller for each VM session.
 * On cancellation failure stop retires the session but propagates the error;
 * it does not prove that an already active physical vibration was cancelled.
 */
internal class HostVibrationControl(private val pulse: (Long) -> Unit, private val cancel: () -> Unit) {
    internal class Session internal constructor()

    private var started = false
    private var current: Session? = null
    private var dispatching = false

    @Synchronized
    fun start(): Session {
        check(!dispatching && !started) { "Vibration controller already used" }
        started = true
        return Session().also { current = it }
    }

    @Synchronized
    fun vibrate(session: Session, durationMs: Long): Boolean {
        check(!dispatching) { "Reentrant vibration request" }
        if (current !== session || durationMs !in 1..MAX_PULSE_MS) return false
        dispatching = true
        try {
            pulse(durationMs)
        } finally {
            dispatching = false
        }
        return true
    }

    @Synchronized
    fun cancel(session: Session): Boolean {
        check(!dispatching) { "Reentrant vibration request" }
        if (current !== session) return false
        dispatching = true
        try {
            cancel.invoke()
        } finally {
            dispatching = false
        }
        return true
    }

    @Synchronized
    fun stop(session: Session): Boolean {
        check(!dispatching) { "Reentrant vibration request" }
        if (current !== session) return false
        current = null
        dispatching = true
        try {
            cancel.invoke()
        } finally {
            dispatching = false
        }
        return true
    }

    companion object {
        const val MAX_PULSE_MS = 5_000L
    }
}
