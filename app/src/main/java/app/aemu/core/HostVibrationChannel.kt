package app.aemu.core

import android.content.Context
import android.net.LocalServerSocket
import android.net.LocalSocket
import android.net.LocalSocketAddress
import android.os.ParcelFileDescriptor
import android.os.Process as AndroidProcess
import android.system.Os
import android.system.OsConstants
import android.system.StructMsghdr
import android.system.StructPollfd
import java.io.Closeable
import java.io.IOException
import java.nio.ByteBuffer
import java.security.SecureRandom
import java.util.UUID

/**
 * Experimental and not started by GuestVm. Authenticating the HAL process does
 * not establish the original Guest application's permission to vibrate.
 */
internal class HostVibrationChannel private constructor(
    val socketName: String,
    val epoch: Long,
    private val context: Context,
    private val listener: LocalSocket,
    private val server: LocalServerSocket,
) : Closeable {
    private val lock = Any()
    private val closeOnce = RadioChannelCloseOnce()
    private var retired = false
    private var attaching = false
    private var candidate: LocalSocket? = null
    private var session: HostVibrationSession? = null

    companion object {
        private const val ATTACH_TIMEOUT_MS = 20_000
        private const val MSG_DONTWAIT = 0x40
        private const val MSG_NOSIGNAL = 0x4000

        internal fun handshake(epoch: Long): ByteArray {
            require(epoch > 0)
            return ByteBuffer.allocate(12).put(byteArrayOf(0x52, 0x4d, 0x45, 0x48)).putLong(epoch).array()
        }

        fun prepare(context: Context): HostVibrationChannel {
            val random = SecureRandom()
            var epoch: Long
            do { epoch = random.nextLong() and Long.MAX_VALUE } while (epoch == 0L)
            val name = "rme-vibration-" + UUID.randomUUID().toString().replace("-", "")
            val listener = LocalSocket(LocalSocket.SOCKET_SEQPACKET)
            var server: LocalServerSocket? = null
            try {
                listener.bind(LocalSocketAddress(name, LocalSocketAddress.Namespace.ABSTRACT))
                Os.setsockoptInt(listener.fileDescriptor, OsConstants.SOL_SOCKET, OsConstants.SO_PASSCRED, 1)
                server = LocalServerSocket(listener.fileDescriptor)
                val flags = Os.fcntlInt(listener.fileDescriptor, OsConstants.F_GETFL, 0)
                Os.fcntlInt(listener.fileDescriptor, OsConstants.F_SETFL, flags or OsConstants.O_NONBLOCK)
                return HostVibrationChannel(name, epoch, context, listener, server)
            } catch (failure: Throwable) {
                try { RadioHostChannel.closeAll({ server?.close() }, { listener.close() }) }
                catch (cleanup: Throwable) { if (cleanup !== failure) failure.addSuppressed(cleanup) }
                throw failure
            }
        }
    }

    /** Accept the launched process only; no PID-only or unauthenticated fallback. */
    fun attach(process: Process): Boolean {
        if (process !is ManagedGuestProcess) return false
        synchronized(lock) {
            if (retired || attaching || session != null) return false
            attaching = true
        }
        var accepted: LocalSocket? = null
        var duplicate: ParcelFileDescriptor? = null
        try {
            val control = HostVibrationDevice.fromContext(context) ?: throw IOException("No host vibrator")
            val poll = StructPollfd().apply { fd = server.fileDescriptor; events = OsConstants.POLLIN.toShort() }
            if (Os.poll(arrayOf(poll), ATTACH_TIMEOUT_MS) <= 0) throw IOException("Vibration connection timeout")
            val peer = synchronized(lock) {
                check(!retired) { "Vibration channel closed" }
                // The listener is nonblocking. Publish the accepted peer while
                // holding the close lock so stop cannot lose its descriptor.
                server.accept().also { accepted = it; candidate = it }
            }
            val creds = peer.peerCredentials
            process.withAuthenticatedHandle { pid, pidfd ->
                check(creds.pid == pid && creds.uid == AndroidProcess.myUid() && HostNative.pidfdSignal(pidfd, 0) == 0) {
                    "Vibration peer identity mismatch"
                }
            }
            Os.setsockoptInt(peer.fileDescriptor, OsConstants.SOL_SOCKET, OsConstants.SO_PASSCRED, 1)
            duplicate = ParcelFileDescriptor.dup(peer.fileDescriptor)
            synchronized(lock) {
                check(!retired) { "Vibration channel closed" }
                val message = handshake(epoch)
                val msg = StructMsghdr(null, arrayOf(ByteBuffer.wrap(message)), null, 0)
                if (Os.sendmsg(peer.fileDescriptor, msg, MSG_DONTWAIT or MSG_NOSIGNAL) != message.size)
                    throw IOException("Incomplete vibration handshake")
                val input = HostVibrationReader(duplicate!!, process, epoch)
                duplicate = null // reader owns the duplicate, including if session construction fails
                session = HostVibrationSession(control, input)
            }
            return true
        } catch (failure: Throwable) {
            val result = RadioHostChannel.finishFailedAttach(failure) { close() }
            android.util.Log.w("HostVibrationChannel", "Vibration connection failed", failure)
            return result
        } finally {
            val release = synchronized(lock) {
                if (candidate === accepted) { candidate = null; accepted } else null
            }
            try { duplicate?.close() } finally {
                try { release?.close() } finally { synchronized(lock) { attaching = false } }
            }
        }
    }

    override fun close() = closeOnce.close {
        val (oldSession, oldCandidate) = synchronized(lock) {
            retired = true
            val resources = session to candidate
            session = null
            candidate = null
            resources
        }
        RadioHostChannel.closeAll(
            { oldSession?.close() }, { oldCandidate?.close() }, { server.close() }, { listener.close() },
        )
    }
}
