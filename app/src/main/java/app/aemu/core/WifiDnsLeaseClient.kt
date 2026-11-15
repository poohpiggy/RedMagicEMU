package app.aemu.core

import java.nio.ByteBuffer
import java.util.concurrent.atomic.AtomicLong

/** Requests DNS authorization from the authenticated Guest observer. */
internal class WifiDnsLeaseClient(private val onUncertain: () -> Unit = {}, private val exchange: (ByteArray) -> RadioSubmitResult) : TargetDnsResolver.GuestNetwork {
    constructor(channel: WifiDnsHostChannel, onUncertain: () -> Unit) : this(onUncertain, channel::verify)
    private val notices=AtomicLong(1)
    private fun request(operation: Int, token: Long, netId: Long): Boolean {
        require(operation in 1..3 && token > 0 && netId in 0..0xffffffffL)
        val body = ByteBuffer.allocate(13).put(operation.toByte()).putLong(token).putInt(netId.toInt()).array()
        return when (exchange(body)) {
            RadioSubmitResult.SUBMITTED -> true
            RadioSubmitResult.REJECTED -> false
            RadioSubmitResult.UNCERTAIN -> {
                val failure = java.io.IOException("Guest DNS verifier exchange uncertain")
                try { onUncertain() } catch (cleanup: Exception) { failure.addSuppressed(cleanup) }
                throw failure
            }
            RadioSubmitResult.UNAVAILABLE -> throw java.io.IOException("Guest DNS verifier unavailable")
        }
    }
    override fun begin(token: Long, id: Long) = request(1, token, id)
    override fun finish(token: Long) = request(2, token, 0)
    fun report(available: Boolean): Boolean {
        val token=notices.getAndIncrement()
        if (token <= 0) throw java.io.IOException("Guest DNS notice sequence exhausted")
        return request(3, token, if (available) 1 else 0)
    }
}
