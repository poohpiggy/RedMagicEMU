package app.aemu.core

/** Serializes exchanges while allowing session retirement to wake queued work. */
internal class WifiDnsExchangeGate {
    private val lock = java.lang.Object()
    private var closed = false
    private var busy = false

    fun <T> run(unavailable: T, late: T, exchange: () -> T): T {
        synchronized(lock) {
            while (busy && !closed) {
                try { lock.wait() } catch (_: InterruptedException) {
                    Thread.currentThread().interrupt()
                    return unavailable
                }
            }
            if (closed) return unavailable
            busy = true
        }
        try {
            val result = exchange()
            return synchronized(lock) { if (closed) late else result }
        } finally {
            synchronized(lock) { busy = false; lock.notifyAll() }
        }
    }

    fun isClosed(): Boolean = synchronized(lock) { closed }

    fun close(interrupt: () -> Unit) {
        synchronized(lock) {
            if (closed) return
            closed = true
            lock.notifyAll()
        }
        interrupt()
    }
}
