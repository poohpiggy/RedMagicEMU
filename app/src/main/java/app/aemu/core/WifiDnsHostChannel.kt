package app.aemu.core

import android.net.LocalServerSocket
import android.net.LocalSocket
import android.net.LocalSocketAddress
import android.os.ParcelFileDescriptor
import android.os.Process as AndroidProcess
import android.os.SystemClock
import android.system.Os
import android.system.OsConstants
import android.system.StructMsghdr
import java.io.Closeable
import java.io.IOException
import java.nio.ByteBuffer
import java.security.SecureRandom
import java.time.ZonedDateTime
import java.util.UUID

/** Authenticated request channel to the launched Wi-Fi DNS verifier. */
internal class WifiDnsHostChannel private constructor(
    val socketName: String,
    val epoch: Long,
    private val listener: LocalSocket,
    private val server: LocalServerSocket,
) : Closeable {
    private val lock = Any()
    private val exchanges = WifiDnsExchangeGate()
    private var socket: LocalSocket? = null
    private var socketFd: ParcelFileDescriptor? = null
    private var owner: ManagedGuestProcess? = null
    private var sequence = 0L
    private var retired = false
    private var attaching = false
    private val closeOnce = RadioChannelCloseOnce()

    companion object {
        internal const val HELLO_SEQUENCE = Long.MAX_VALUE
        private const val TIMEOUT_MS = 5000L
        private const val ATTACH_TIMEOUT_MS = 20000L
        private const val MSG_DONTWAIT = 0x40
        private const val MSG_NOSIGNAL = 0x4000

        /** Retire the connection first; fatal Errors propagate after cleanup. */
        internal fun finishFailedAttach(failure: Throwable, cleanup: () -> Unit): Boolean {
            try { cleanup() } catch (cleanupFailure: Throwable) {
                if (failure is Error && failure !is LinkageError && cleanupFailure !is Error) {
                    if (cleanupFailure !== failure) failure.addSuppressed(cleanupFailure)
                    throw failure
                }
                if (cleanupFailure !== failure) cleanupFailure.addSuppressed(failure)
                throw cleanupFailure
            }
            if (failure is Exception || failure is LinkageError) return false
            throw failure
        }

        internal fun finishFailedSubmit(failure: Throwable, sent: Boolean, cleanup: () -> Unit): RadioSubmitResult {
            finishFailedAttach(failure, cleanup)
            return if (sent) RadioSubmitResult.UNCERTAIN else RadioSubmitResult.UNAVAILABLE
        }

        /** Attempt every close; a fatal Error takes precedence over ordinary close failures. */
        internal fun closeAll(vararg actions: () -> Unit) {
            var failure: Throwable? = null
            for (action in actions) {
                try { action() } catch (error: Throwable) {
                    if (failure == null) failure = error
                    else if (failure !== error) {
                        if (error is Error && failure !is Error) { error.addSuppressed(failure); failure = error }
                        else failure.addSuppressed(error)
                    }
                }
            }
            if (failure != null) throw failure
        }

        fun prepare(): WifiDnsHostChannel {
            val random = SecureRandom()
            var epoch: Long
            do { epoch = random.nextLong() and Long.MAX_VALUE } while (epoch == 0L)
            val name = "rme-wifi-dns-" + UUID.randomUUID().toString().replace("-", "")
            val listener = LocalSocket(LocalSocket.SOCKET_SEQPACKET)
            try {
                listener.bind(LocalSocketAddress(name, LocalSocketAddress.Namespace.ABSTRACT))
                Os.setsockoptInt(listener.fileDescriptor, OsConstants.SOL_SOCKET, OsConstants.SO_PASSCRED, 1)
                val server = LocalServerSocket(listener.fileDescriptor)
                val flags = Os.fcntlInt(listener.fileDescriptor, OsConstants.F_GETFL, 0)
                Os.fcntlInt(listener.fileDescriptor, OsConstants.F_SETFL, flags or OsConstants.O_NONBLOCK)
                return WifiDnsHostChannel(name, epoch, listener, server)
            } catch (failure: Throwable) { listener.close(); throw failure }
        }

        fun frame(epoch: Long, sequence: Long, kind: Int, payload: ByteArray): ByteArray {
            require(epoch > 0 && sequence > 0 && sequence < HELLO_SEQUENCE && kind in 1..2 && payload.size in 1..256)
            return ByteBuffer.allocate(23 + payload.size).put(byteArrayOf(82, 77, 69, 49))
                .putLong(epoch).putLong(sequence).put(kind.toByte()).putShort(payload.size.toShort())
                .put(payload).array()
        }
    }

    /** Authenticates the actual launched child and its first packet; never falls back to PID alone. */
    fun attach(process: Process): Boolean {
        if (process !is ManagedGuestProcess) return false
        synchronized(lock) {
            if (retired || socket != null || attaching) return false
            attaching = true
        }
        var candidate: LocalSocket? = null
        var duplicate: ParcelFileDescriptor? = null
        try {
            val deadline = SystemClock.elapsedRealtime() + ATTACH_TIMEOUT_MS
            val poll = android.system.StructPollfd().apply {
                fd = server.fileDescriptor; events = OsConstants.POLLIN.toShort()
            }
            if (Os.poll(arrayOf(poll), ATTACH_TIMEOUT_MS.toInt()) <= 0) throw IOException("Wi-Fi DNS verifier connection timeout")
            candidate = server.accept()
            val creds = candidate.peerCredentials
            val uid = AndroidProcess.myUid()
            process.withAuthenticatedHandle { pid, pidfd ->
                check(creds.pid == pid && creds.uid == uid && HostNative.pidfdSignal(pidfd, 0) == 0) {
                    "Wi-Fi DNS verifier peer identity mismatch"
                }
            }
            Os.setsockoptInt(candidate.fileDescriptor, OsConstants.SOL_SOCKET, OsConstants.SO_PASSCRED, 1)
            duplicate = ParcelFileDescriptor.dup(candidate.fileDescriptor)
            var result: Int
            do {
                result = process.withAuthenticatedHandle { pid, pidfd ->
                    HostNative.radioReadAck(duplicate!!.fd, pidfd, pid, uid, epoch, HELLO_SEQUENCE)
                }
                if (result == 0) Thread.sleep(10)
            } while (result == 0 && SystemClock.elapsedRealtime() < deadline)
            if (result != 1) throw IOException("Wi-Fi DNS verifier hello rejected or timed out")
            synchronized(lock) {
                check(!retired) { "Wi-Fi DNS verifier connection closed" }
                socket = candidate; socketFd = duplicate; owner = process
                candidate = null; duplicate = null
                server.close(); listener.close()
            }
            return true
        } catch (failure: Throwable) {
            val result = finishFailedAttach(failure) {
                try { duplicate?.close() } finally {
                    try { candidate?.close() } finally { synchronized(lock) { retireLocked() } }
                }
            }
            android.util.Log.w("WifiDnsHostChannel", "Wi-Fi DNS verifier connection failed", failure)
            return result
        } finally {
            synchronized(lock) { attaching = false }
        }
    }

    fun verify(payload: ByteArray): RadioSubmitResult = submit(1, payload)

    private data class Exchange(val descriptor: ParcelFileDescriptor, val owner: ManagedGuestProcess,
                                val sequence: Long, val message: ByteArray)

    private fun submit(kind: Int, payload: ByteArray): RadioSubmitResult =
        exchanges.run(RadioSubmitResult.UNAVAILABLE, RadioSubmitResult.UNCERTAIN) {
            val pending = synchronized(lock) {
                val descriptor = socketFd ?: return@run RadioSubmitResult.UNAVAILABLE
                val process = owner ?: return@run RadioSubmitResult.UNAVAILABLE
                if (retired || sequence >= HELLO_SEQUENCE - 1) return@run RadioSubmitResult.UNAVAILABLE
                val next = sequence + 1
                val message = frame(epoch, next, kind, payload)
                // The request owns a stable duplicate. Closing the session cannot cause
                // the OS to reuse this descriptor while the request is polling it.
                val duplicate = try { ParcelFileDescriptor.dup(descriptor.fileDescriptor) }
                catch (failure: Exception) { return@run RadioSubmitResult.UNAVAILABLE }
                sequence = next
                Exchange(duplicate, process, next, message)
            }
            var sent = false
            try {
                pending.owner.withAuthenticatedHandle { _, pidfd ->
                    check(HostNative.pidfdSignal(pidfd, 0) == 0) { "Wi-Fi DNS verifier process exited" }
                }
                if (exchanges.isClosed()) return@run RadioSubmitResult.UNAVAILABLE
                val msg = StructMsghdr(null, arrayOf(ByteBuffer.wrap(pending.message)), null, 0)
                if (Os.sendmsg(pending.descriptor.fileDescriptor, msg, MSG_DONTWAIT or MSG_NOSIGNAL) != pending.message.size)
                    throw IOException("incomplete Wi-Fi DNS verifier packet")
                sent = true
                val deadline = SystemClock.elapsedRealtime() + TIMEOUT_MS
                var result: Int
                do {
                    if (exchanges.isClosed()) return@run RadioSubmitResult.UNCERTAIN
                    result = pending.owner.withAuthenticatedHandle { pid, pidfd ->
                        HostNative.radioReadAck(pending.descriptor.fd, pidfd, pid, AndroidProcess.myUid(), epoch, pending.sequence)
                    }
                    if (result == 0) Thread.sleep(10)
                } while (result == 0 && SystemClock.elapsedRealtime() < deadline)
                when (result) {
                    1 -> RadioSubmitResult.SUBMITTED
                    2 -> RadioSubmitResult.REJECTED
                    else -> { retireChannel(); RadioSubmitResult.UNCERTAIN }
                }
            } catch (failure: Throwable) {
                val result = finishFailedSubmit(failure, sent) { retireChannel() }
                android.util.Log.w("WifiDnsHostChannel", "Wi-Fi DNS verifier delivery failed", failure)
                result
            } finally {
                pending.descriptor.close()
            }
        }

    private fun retireChannel() = exchanges.close { synchronized(lock) { retireLocked() } }

    private fun retireLocked() {
        retired = true
        closeOnce.close {
            val oldDescriptor = socketFd; socketFd = null
            val oldSocket = socket; socket = null
            owner = null
            closeAll({ oldDescriptor?.close() }, { oldSocket?.close() }, { server.close() }, { listener.close() })
        }
    }

    override fun close() = retireChannel()
}
