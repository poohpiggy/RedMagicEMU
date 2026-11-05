package app.aemu.core

import android.net.LocalServerSocket
import android.os.Process as AndroidProcess
import android.system.Os
import android.system.OsConstants
import android.system.StructPollfd
import java.io.DataInputStream
import java.io.File
import java.security.SecureRandom
import java.util.UUID
import java.util.concurrent.TimeUnit

/** Starts a guest with a pidfd only after authenticating its pre-exec launcher. */
internal class PidfdGuestLauncher(
    private val nativeDir: File,
    private val log: (String) -> Unit,
    private val processPid: (Process) -> Int?,
) {
    private val random = SecureRandom()
    private val available: Boolean by lazy {
        if (!HostNative.available) {
            log("stop: pidfd unavailable; ordinary termination only")
            false
        } else {
            // Some seccomp policies terminate the calling process instead of returning EPERM.
            // Probe in a disposable process before invoking either syscall in the VM.
            val supported = runCatching {
                val probe = ProcessBuilder(File(nativeDir, "libaemu-launch.so").absolutePath, "--probe-pidfd")
                    .redirectErrorStream(true).redirectOutput(File("/dev/null")).start()
                probe.waitFor(5, TimeUnit.SECONDS) && probe.exitValue() == 0
            }.getOrDefault(false)
            if (!supported) log("stop: pidfd probe failed; ordinary termination only")
            supported
        }
    }

    private class Unsupported(val errno: Int) : Exception()

    fun start(builder: ProcessBuilder): Process {
        if (!available) return builder.start()
        val original = builder.command().toList()
        val name = "rme-" + UUID.randomUUID().toString().replace("-", "")
        val server = LocalServerSocket(name)
        var child: Process? = null
        var handle = -1
        var verified = false
        try {
            builder.command(listOf(File(nativeDir, "libaemu-launch.so").absolutePath, name) + original)
            val p = builder.start()
            child = p
            val poll = StructPollfd().apply { fd = server.fileDescriptor; events = OsConstants.POLLIN.toShort() }
            check(Os.poll(arrayOf(poll), 5_000) > 0) { "Guest launcher connection timed out" }
            server.accept().use { peer ->
                peer.soTimeout = 5_000
                val creds = peer.peerCredentials
                val expected = processPid(p) ?: error("Guest launcher PID is unavailable")
                check(creds.pid == expected && creds.uid == AndroidProcess.myUid()) { "Guest launcher peer identity mismatch" }
                handle = HostNative.pidfdOpen(creds.pid)
                if (handle < 0) throw Unsupported(-handle)
                val probe = HostNative.pidfdSignal(handle, 0)
                if (probe != 0) throw Unsupported(-probe)
                val challenge = ByteArray(32).also(random::nextBytes)
                peer.outputStream.write(challenge)
                peer.outputStream.flush()
                val response = ByteArray(challenge.size)
                DataInputStream(peer.inputStream).readFully(response)
                check(response.contentEquals(challenge)) { "Guest launcher challenge mismatch" }
                verified = true
                peer.outputStream.write('G'.code)
                peer.outputStream.flush()
            }
            val result = ManagedGuestProcess(p, processPid(p) ?: error("Guest launcher PID is unavailable"), handle,
                HostNative::pidfdSignal, HostNative::pidfdClose)
            handle = -1
            return result
        } catch (failure: Throwable) {
            if (verified && handle >= 0) {
                val result = HostNative.pidfdSignal(handle, 9)
                check(result == 0 || result == -3) { "Guest launcher cleanup failed: errno=${-result}" }
            }
            server.close()
            val exited = child?.waitFor(6, TimeUnit.SECONDS) ?: true
            check(exited) { "Guest launcher did not exit after failed authentication" }
            if (failure is Unsupported) {
                log("stop: guest pidfd unavailable (errno=${failure.errno}); ordinary termination only")
                builder.command(original)
                return builder.start()
            }
            throw failure
        } finally {
            if (handle >= 0) HostNative.pidfdClose(handle)
            server.close()
        }
    }
}
