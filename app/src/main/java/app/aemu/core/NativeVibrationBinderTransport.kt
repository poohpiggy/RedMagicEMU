package app.aemu.core

import android.net.LocalSocket
import android.net.LocalSocketAddress
import android.os.ParcelFileDescriptor
import android.os.Process as AndroidProcess
import java.io.File
import java.io.IOException
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

/** Caller owns these descriptors; nativeOpen duplicates them before this lease closes. */
internal interface VibrationBinderDescriptors : AutoCloseable {
    val socketFd: Int
    val pidfd: Int
    val pid: Int
    val uid: Int
}

internal interface VibrationBinderNativeCalls {
    fun open(socket: Int, pidfd: Int, pid: Int, uid: Int, header: ByteArray): Long
    fun next(handle: Long): LongArray?
    fun complete(handle: Long, token: Long, success: Boolean): Int
    fun stop(handle: Long): Int
    fun destroy(handle: Long): Int
}

private object ExperimentalVibrationBinderCalls : VibrationBinderNativeCalls {
    override fun open(socket: Int, pidfd: Int, pid: Int, uid: Int, header: ByteArray) =
        VibrationBinderExperimentalNative.nativeOpen(socket, pidfd, pid, uid, header)
    override fun next(handle: Long) = VibrationBinderExperimentalNative.nativeNext(handle)
    override fun complete(handle: Long, token: Long, success: Boolean) =
        VibrationBinderExperimentalNative.nativeComplete(handle, token, success)
    override fun stop(handle: Long) = VibrationBinderExperimentalNative.nativeStop(handle)
    override fun destroy(handle: Long) = VibrationBinderExperimentalNative.nativeDestroy(handle)
}

/** Experimental transport only: neither service discovery nor Guest caller authorization is proven. */
internal class NativeVibrationBinderTransport private constructor(
    private val native: VibrationBinderNativeCalls,
    handle: Long,
) : HostVibrationBinderTransport {
    private val lock = Any()
    private var handle = handle
    private var closeFailure: Throwable? = null
    private val registrationFinished = CountDownLatch(1)
    private var registered = false

    companion object {
        /** Always closes the passed descriptor lease, even if authentication or opening fails. */
        internal fun open(descriptors: VibrationBinderDescriptors, header: ByteArray,
                          native: VibrationBinderNativeCalls): NativeVibrationBinderTransport {
            var handle = 0L
            try {
                descriptors.use { source ->
                    require(header.size == 12 && header[8] == 0x54.toByte() && header[9] == 0x53.toByte() &&
                        header[10] == 0x59.toByte() && header[11] == 0x53.toByte()) { "Invalid Binder header" }
                    require(source.socketFd >= 0 && source.pidfd >= 0 && source.pid > 0 && source.uid >= 0)
                    handle = native.open(source.socketFd, source.pidfd, source.pid, source.uid, header.copyOf())
                    if (handle <= 0) throw IOException("Vibration Binder connection rejected")
                }
                return NativeVibrationBinderTransport(native, handle)
            } catch (failure: Throwable) {
                if (handle != 0L) try {
                    if (native.destroy(handle) != 0) failure.addSuppressed(IOException("Native handle cleanup failed"))
                } catch (cleanup: Throwable) { if (cleanup !== failure) failure.addSuppressed(cleanup) }
                throw failure
            }
        }

        /** Connects only to the launcher-authenticated Binder process; no pid-only fallback. */
        internal fun connect(process: ManagedGuestProcess, socketPath: File, header: ByteArray): NativeVibrationBinderTransport {
            require(socketPath.isAbsolute) { "Binder socket path must be absolute" }
            check(HostNative.available && VibrationBinderExperimentalNative.available) { "Vibration Binder native support unavailable" }
            // Borrow only long enough to check and duplicate the pidfd. No socket I/O while borrowed.
            val (pid, pidfd) = process.withAuthenticatedHandle { physicalPid, borrowedFd ->
                check(HostNative.pidfdSignal(borrowedFd, 0) == 0) { "Binder process exited" }
                physicalPid to ParcelFileDescriptor.fromFd(borrowedFd)
            }
            pidfd.use { ownedPidfd ->
                LocalSocket(LocalSocket.SOCKET_SEQPACKET).use { socket ->
                    socket.connect(LocalSocketAddress(socketPath.absolutePath, LocalSocketAddress.Namespace.FILESYSTEM))
                    ParcelFileDescriptor.dup(socket.fileDescriptor).use { ownedSocket ->
                        val descriptors = object : VibrationBinderDescriptors {
                            override val socketFd = ownedSocket.fd
                            override val pidfd = ownedPidfd.fd
                            override val pid = pid
                            override val uid = AndroidProcess.myUid()
                            // The surrounding use blocks retain and close these descriptors.
                            override fun close() = Unit
                        }
                        return open(descriptors, header, ExperimentalVibrationBinderCalls)
                    }
                }
            }
        }
    }

    /** A zero addService reply only; it does not confirm discovery or the service owner. */
    internal fun awaitRegistration(timeoutMs: Long): Boolean {
        require(timeoutMs >= 0)
        if (!registrationFinished.await(timeoutMs, TimeUnit.MILLISECONDS)) return false
        return synchronized(lock) { registered && handle != 0L }
    }

    override fun next(): HostVibrationBinderEvent? {
        try {
            while (true) {
                val active = synchronized(lock) { handle }
                if (active == 0L) return null
                val values = native.next(active) ?: throw IOException("Vibration Binder receive failed")
                if (values.size != 4) throw IOException("Invalid vibration Binder response")
                synchronized(lock) { if (handle != active) return null }
                if (values[0] == 3L && values[1] == 0L && values[2] == 0L && values[3] == 0L) {
                    synchronized(lock) {
                        if (handle != active) return null
                        if (registered) throw IOException("Duplicate vibration registration reply")
                        registered = true
                        registrationFinished.countDown()
                    }
                    continue
                }
                if (values[0] == 1L && values[1] == 0L && values[2] == 0L && values[3] == 0L) {
                    synchronized(lock) { registered = false; registrationFinished.countDown() }
                    return null
                }
                val token = values[1]; val code = values[2]; val duration = values[3]
                if (values[0] != 2L || token <= 0 ||
                    !(code == 1L && duration == 0L || code == 2L && duration == 0L ||
                        code == 3L && duration in 1..HostVibrationControl.MAX_PULSE_MS))
                    throw IOException("Invalid vibration Binder event")
                synchronized(lock) { if (!registered) throw IOException("Vibration service not registered") }
                return HostVibrationBinderEvent(token, code.toInt(), duration)
            }
        } catch (failure: Throwable) {
            synchronized(lock) { registered = false; registrationFinished.countDown() }
            throw failure
        }
    }

    override fun complete(token: Long, success: Boolean) = synchronized(lock) {
        if (handle == 0L || token <= 0 || native.complete(handle, token, success) != 0)
            throw IOException("Vibration Binder reply failed or stopped")
    }

    override fun requestStop() = synchronized(lock) {
        if (handle != 0L && native.stop(handle) != 0) throw IOException("Vibration Binder stop failed")
    }

    override fun close() = synchronized(lock) {
        closeFailure?.let { throw it }
        val old = handle
        if (old != 0L) {
            handle = 0
            registrationFinished.countDown()
            try {
                if (native.destroy(old) != 0) throw IOException("Vibration Binder cleanup failed")
            } catch (failure: Throwable) { closeFailure = failure; throw failure }
        }
    }
}
