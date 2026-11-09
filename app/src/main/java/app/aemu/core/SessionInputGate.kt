package app.aemu.core

/**
 * Serializes short input actions with session replacement and invalidation.
 * A caller must obtain a permit when an action is queued and dispatch the
 * actual action through this gate. Actions must be short. Session lifecycle
 * changes are rejected when called from an action. Work sent elsewhere after
 * dispatch is not protected by the gate; neither are independent input paths.
 *
 * This primitive is not connected to input services. In particular it does
 * not enforce permissions or coordinate Guest-side asynchronous writes.
 */
internal class SessionInputGate {
    internal class Session internal constructor()
    internal class Permit internal constructor(internal val session: Session)

    private var current: Session? = null
    private var blocked = true
    private var dispatchDepth = 0

    @Synchronized
    fun begin(blocked: Boolean): Session {
        check(dispatchDepth == 0) { "Cannot change session during input dispatch" }
        val session = Session()
        current = session
        this.blocked = blocked
        return session
    }

    @Synchronized
    fun capture(): Permit? {
        val session = current ?: return null
        return if (blocked) null else Permit(session)
    }

    @Synchronized
    fun dispatch(permit: Permit?, action: () -> Unit): Boolean {
        if (permit == null || blocked || current !== permit.session) return false
        dispatchDepth++
        try {
            action()
        } finally {
            dispatchDepth--
        }
        return true
    }

    @Synchronized
    fun open(session: Session): Boolean {
        check(dispatchDepth == 0) { "Cannot change session during input dispatch" }
        if (current !== session) return false
        blocked = false
        return true
    }

    @Synchronized
    fun stop(session: Session): Boolean {
        check(dispatchDepth == 0) { "Cannot change session during input dispatch" }
        if (current !== session) return false
        current = null
        blocked = true
        return true
    }
}
