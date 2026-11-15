package app.aemu.core

import java.io.Closeable
import java.util.concurrent.atomic.AtomicBoolean

/** Bounded DNS clients; old workers retain slots until they actually exit. */
internal class DnsClientAdmission(private val maximum: Int, private val onCloseError: (Throwable) -> Unit = {}) {
    init { require(maximum > 0) }
    private val lock = Any()
    private var running = false
    private var generation = 0L
    private val active = LinkedHashSet<Lease>()

    fun start(): Long = synchronized(lock) {
        check(generation < Long.MAX_VALUE) { "DNS server generation exhausted" }
        generation++
        running = true
        generation
    }
    fun tryAdmit(client: Closeable, expectedGeneration: Long): Lease? {
        val result = synchronized(lock) {
            if (!running || generation != expectedGeneration || active.size >= maximum) null
            else Lease(client).also { active.add(it) }
        }
        if (result == null) closeClient(client)
        return result
    }
    fun stop() {
        val clients = synchronized(lock) { running = false; active.toList() }
        clients.forEach { it.stopClient() }
    }
    private fun closeClient(client: Closeable) {
        try { client.close() } catch (failure: Exception) { onCloseError(failure) }
    }
    inner class Lease internal constructor(private val client: Closeable) : Closeable {
        private val clientClosed = AtomicBoolean()
        private val released = AtomicBoolean()
        internal fun stopClient() { if (clientClosed.compareAndSet(false, true)) closeClient(client) }
        override fun close() {
            if (released.compareAndSet(false, true)) {
                try { stopClient() } finally { synchronized(lock) { active.remove(this) } }
            }
        }
    }
}
