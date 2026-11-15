package app.aemu.core

import java.io.Closeable
import java.util.concurrent.Semaphore
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicLong

internal class TargetDnsResolver(private val networks: HostNetworks, private val guest: GuestNetwork,
        private val raw: (Any, ByteArray, Int) -> DnsRawProtocol.Result) : Closeable {
    data class HostNetwork(val network: Any, val generation: Long)
    interface HostNetworks { fun snapshot(): HostNetwork?; fun isCurrent(snapshot: HostNetwork): Boolean }
    interface GuestNetwork { fun begin(token: Long, id: Long): Boolean; fun finish(token: Long): Boolean }
    private val tokens = AtomicLong(1)
    // Include authentication and final validation, not just the host DNS operation.
    private val admissions = Semaphore(32)
    private val closed = AtomicBoolean(false)
    override fun close() { closed.set(true) }
    fun query(id: Long, packet: ByteArray, flags: Int): DnsRawProtocol.Result {
        if (id < 0 || id > 0xffffffffL) return DnsRawProtocol.Result.Error(22 /* EINVAL */)
        if (closed.get()) return DnsRawProtocol.Result.Error(64 /* ENONET */)
        if (!admissions.tryAcquire()) return DnsRawProtocol.Result.Error(16 /* EBUSY */)
        try {
            if (closed.get()) return DnsRawProtocol.Result.Error(64 /* ENONET */)
            val selected = try { networks.snapshot() } catch (_: Exception) { null }
                ?: return DnsRawProtocol.Result.Error(64 /* ENONET */)
            val token = tokens.getAndIncrement()
            if (token <= 0) return DnsRawProtocol.Result.Error(16 /* EBUSY */)
            if (!(try { guest.begin(token, id) } catch (_: Exception) { false }))
                return DnsRawProtocol.Result.Error(64 /* ENONET */)
            // A successful begin owns a Guest lease even if the resolver was retired.
            var stillGuest = false
            val answer = try {
                if (closed.get()) DnsRawProtocol.Result.Error(64 /* ENONET */)
                else try { raw(selected.network, packet, flags) }
                catch (_: Exception) { DnsRawProtocol.Result.Error(5 /* EIO */) }
            } finally {
                stillGuest = try { guest.finish(token) } catch (_: Exception) { false }
            }
            val stillHost = try { networks.isCurrent(selected) } catch (_: Exception) { false }
            if (closed.get() || !stillGuest || !stillHost) return DnsRawProtocol.Result.Error(64 /* ENONET */)
            return answer
        } finally {
            admissions.release()
        }
    }
}
