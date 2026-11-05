package app.aemu.core

/** Serializes boot, cancellation and process creation for one VM. */
internal class VmLifecycle {
    private val lock = java.lang.Object()
    private var active = false
    private var booting = false
    private var stopping = false
    private var workers = 0

    fun begin(onBegin: () -> Unit = {}): Boolean = synchronized(lock) {
        if (active) return false
        active = true
        booting = true
        stopping = false
        onBegin()
        true
    }

    fun endBoot() = synchronized(lock) {
        booting = false
        lock.notifyAll()
    }

    fun ifRunning(action: () -> Unit) = synchronized(lock) {
        if (active && !stopping) action()
    }

    fun <T> create(action: () -> T): T = synchronized(lock) {
        check(active && !stopping) { "VM is stopping or stopped" }
        action()
    }

    fun <T> run(action: () -> T): T {
        synchronized(lock) {
            check(active && !stopping) { "VM is stopping or stopped" }
            workers++
        }
        try { return action() } finally {
            synchronized(lock) { workers--; lock.notifyAll() }
        }
    }

    fun launch(name: String, action: () -> Unit) { synchronized(lock) {
        if (!active || stopping) return@synchronized
        workers++
        try {
            Thread({
                try { action() } finally {
                    synchronized(lock) { workers--; lock.notifyAll() }
                }
            }, name).apply { isDaemon = true; start() }
        } catch (t: Throwable) {
            workers--
            lock.notifyAll()
            throw t
        }
    } }

    fun stop(onRequest: () -> Unit, cleanup: () -> Unit) {
        var interrupted = false
        synchronized(lock) {
            while (stopping && active) {
                try { lock.wait() } catch (_: InterruptedException) { interrupted = true }
            }
            if (!active) {
                if (interrupted) Thread.currentThread().interrupt()
                return
            }
            stopping = true
            try {
                onRequest()
            } catch (t: Throwable) {
                // Keep active set: resources may still exist, so a later stop must retry cleanup.
                stopping = false
                lock.notifyAll()
                if (interrupted) Thread.currentThread().interrupt()
                throw t
            }
            while (booting || workers != 0) {
                try { lock.wait() } catch (_: InterruptedException) { interrupted = true }
            }
        }
        var cleaned = false
        try { cleanup(); cleaned = true } finally {
            synchronized(lock) { if (cleaned) active = false; stopping = false; lock.notifyAll() }
            if (interrupted) Thread.currentThread().interrupt()
        }
    }
}
