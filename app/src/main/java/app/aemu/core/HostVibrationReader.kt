package app.aemu.core

import android.os.Process as AndroidProcess
import android.os.ParcelFileDescriptor

/**
 * Reads from an already-connected dedicated SEQPACKET socket. The socket must
 * belong to the VM session. Ownership transfers to this reader only when
 * construction succeeds; the caller must close the duplicate on failure and
 * must not close it separately while the session is running.
 * The kernel gate validates this launched process and each packet. That does
 * not prove that Guest framework permission checks were enforced.
 */
internal class HostVibrationReader(
    private val socket: ParcelFileDescriptor,
    private val owner: ManagedGuestProcess,
    private val epoch: Long,
) : HostVibrationInput, AutoCloseable {
    init { require(socket.fd >= 0 && epoch > 0) }

    @Synchronized override fun read(sequence: Long): Int = owner.withAuthenticatedHandle { pid, pidfd ->
        HostNative.vibrationRead(socket.fd, pidfd, pid, AndroidProcess.myUid(), epoch, sequence)
    }

    @Synchronized override fun close() = socket.close()
}
