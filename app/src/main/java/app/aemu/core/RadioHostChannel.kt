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

/** SUBMITTED is the Radio service's acknowledgement, not proof the user received it. */
internal enum class RadioSubmitResult { SUBMITTED, REJECTED, UNCERTAIN, UNAVAILABLE }

/** The captured channel belongs to this process, even if a later VM session starts. */
internal fun waitForRadioProcess(process: Process, channel: Closeable): Int {
    val code = process.waitFor()
    channel.close()
    return code
}

/** Close the current channel and return any ordinary cleanup failure. */
internal fun closeRadioChannelForStop(channel: Closeable?, clear: () -> Unit): Exception? {
    return try {
        channel?.close()
        clear()
        null
    } catch (error: Exception) { error }
}

/** A close attempt owns all resources even when an individual close fails. */
internal class RadioChannelCloseOnce {
    private var attempted = false
    private var failure: Throwable? = null
    @Synchronized fun close(action: () -> Unit) {
        if (attempted) {
            failure?.let { throw it }
            return
        }
        attempted = true
        try { action() } catch (error: Throwable) {
            failure = error
            throw error
        }
    }
}

/** One authenticated local event connection to the launched Radio process. */
internal class RadioHostChannel private constructor(
    val socketName: String,
    val epoch: Long,
    private val listener: LocalSocket,
    private val server: LocalServerSocket,
) : Closeable {
    private val lock = Any()
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

        fun prepare(): RadioHostChannel {
            val random = SecureRandom()
            var epoch: Long
            do { epoch = random.nextLong() and Long.MAX_VALUE } while (epoch == 0L)
            val name = "rme-radio-" + UUID.randomUUID().toString().replace("-", "")
            val listener = LocalSocket(LocalSocket.SOCKET_SEQPACKET)
            try {
                listener.bind(LocalSocketAddress(name, LocalSocketAddress.Namespace.ABSTRACT))
                Os.setsockoptInt(listener.fileDescriptor, OsConstants.SOL_SOCKET, OsConstants.SO_PASSCRED, 1)
                val server = LocalServerSocket(listener.fileDescriptor)
                val flags = Os.fcntlInt(listener.fileDescriptor, OsConstants.F_GETFL, 0)
                Os.fcntlInt(listener.fileDescriptor, OsConstants.F_SETFL, flags or OsConstants.O_NONBLOCK)
                return RadioHostChannel(name, epoch, listener, server)
            } catch (failure: Throwable) { listener.close(); throw failure }
        }

        fun validNumber(number: String): Boolean {
            val digits = if (number.startsWith('+')) number.substring(1) else number
            return digits.length in 1..32 && digits.all { it in '0'..'9' }
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
            if (Os.poll(arrayOf(poll), ATTACH_TIMEOUT_MS.toInt()) <= 0) throw IOException("Radio connection timeout")
            candidate = server.accept()
            val creds = candidate.peerCredentials
            val uid = AndroidProcess.myUid()
            process.withAuthenticatedHandle { pid, pidfd ->
                check(creds.pid == pid && creds.uid == uid && HostNative.pidfdSignal(pidfd, 0) == 0) {
                    "Radio peer identity mismatch"
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
            if (result != 1) throw IOException("Radio hello rejected or timed out")
            synchronized(lock) {
                check(!retired) { "Radio connection closed" }
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
            android.util.Log.w("RadioHostChannel", "Radio event connection failed", failure)
            return result
        } finally {
            synchronized(lock) { attaching = false }
        }
    }

    fun sms(from: String, body: String): RadioSubmitResult {
        val pdu = try { VirtualSmsDeliver.encode(from.trim(), body, ZonedDateTime.now()) }
        catch (_: IllegalArgumentException) { return RadioSubmitResult.REJECTED }
        return submit(1, pdu)
    }

    fun call(from: String): RadioSubmitResult {
        val number = from.trim()
        if (!validNumber(number)) return RadioSubmitResult.REJECTED
        return submit(2, number.toByteArray(Charsets.US_ASCII))
    }

    private fun submit(kind: Int, payload: ByteArray): RadioSubmitResult = synchronized(lock) {
        val peer = socket ?: return RadioSubmitResult.UNAVAILABLE
        val descriptor = socketFd ?: return RadioSubmitResult.UNAVAILABLE
        val process = owner ?: return RadioSubmitResult.UNAVAILABLE
        if (retired || sequence >= HELLO_SEQUENCE - 1) { retireLocked(); return RadioSubmitResult.UNAVAILABLE }
        val next = sequence + 1
        val message = frame(epoch, next, kind, payload)
        try {
            process.withAuthenticatedHandle { _, pidfd ->
                check(HostNative.pidfdSignal(pidfd, 0) == 0) { "Radio process exited" }
            }
        } catch (failure: Throwable) {
            val result = finishFailedSubmit(failure, sent = false) { retireLocked() }
            android.util.Log.w("RadioHostChannel", "Radio process unavailable", failure)
            return result
        }
        sequence = next
        try {
            val msg = StructMsghdr(null, arrayOf(ByteBuffer.wrap(message)), null, 0)
            if (Os.sendmsg(peer.fileDescriptor, msg, MSG_DONTWAIT or MSG_NOSIGNAL) != message.size)
                throw IOException("incomplete Radio event packet")
            val deadline = SystemClock.elapsedRealtime() + TIMEOUT_MS
            var result: Int
            do {
                result = process.withAuthenticatedHandle { pid, pidfd ->
                    HostNative.radioReadAck(descriptor.fd, pidfd, pid, AndroidProcess.myUid(), epoch, next)
                }
                if (result == 0) Thread.sleep(10)
            } while (result == 0 && SystemClock.elapsedRealtime() < deadline)
            when (result) {
                1 -> RadioSubmitResult.SUBMITTED
                2 -> RadioSubmitResult.REJECTED
                else -> { retireLocked(); RadioSubmitResult.UNCERTAIN }
            }
        } catch (failure: Throwable) {
            val result = finishFailedSubmit(failure, sent = true) { retireLocked() }
            android.util.Log.w("RadioHostChannel", "Radio event delivery uncertain", failure)
            result
        }
    }

    private fun retireLocked() {
        retired = true
        closeOnce.close {
            val oldDescriptor = socketFd; socketFd = null
            val oldSocket = socket; socket = null
            owner = null
            closeAll({ oldDescriptor?.close() }, { oldSocket?.close() }, { server.close() }, { listener.close() })
        }
    }

    override fun close() = synchronized(lock) { retireLocked() }
}
