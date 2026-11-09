package app.aemu.core

import android.net.LocalServerSocket
import android.os.Process as AndroidProcess
import android.system.Os
import android.system.OsConstants
import android.system.StructPollfd
import java.io.DataInputStream
import java.io.File
import java.io.InputStream
import java.io.OutputStream
import java.security.SecureRandom
import java.util.UUID
import java.util.concurrent.TimeUnit

/** The optional I confirms limited experimental rules only, not safe hardware isolation. */
internal fun commitLauncherProtocol(output: OutputStream, input: InputStream, expectLimitedRules: Boolean) {
    output.write('G'.code)
    output.flush()
    if (expectLimitedRules) check(input.read() == 'I'.code) { "limited vibration rules not reported" }
}

/** The experimental launcher is separate and must never fall back to the ordinary one. */
internal fun selectGuestLauncher(nativeDir: File, experimentalVibration: Boolean): File {
    val launcher = File(nativeDir, if (experimentalVibration) "libaemu-vibration-launch.so" else "libaemu-launch.so")
    if (experimentalVibration) check(launcher.isFile && launcher.canExecute()) {
        "Experimental vibration launcher is missing or not executable"
    }
    return launcher
}

/** Starts a guest with a pidfd only after authenticating its pre-exec launcher. */
internal class PidfdGuestLauncher(
    private val nativeDir: File,
    private val log: (String) -> Unit,
    private val processPid: (Process) -> Int?,
    private val availabilityOverride: (() -> Boolean)? = null,
) {
    private val random = SecureRandom()
    private val available: Boolean by lazy {
        if (availabilityOverride != null) availabilityOverride.invoke()
        else if (!HostNative.available) {
            log("stop: pidfd unavailable")
            false
        } else {
            // Some seccomp policies terminate the calling process instead of returning EPERM.
            // Probe in a disposable process before invoking either syscall in the VM.
            val supported = runCatching {
                val probe = ProcessBuilder(File(nativeDir, "libaemu-launch.so").absolutePath, "--probe-pidfd")
                    .redirectErrorStream(true).redirectOutput(File("/dev/null")).start()
                probe.waitFor(5, TimeUnit.SECONDS) && probe.exitValue() == 0
            }.getOrDefault(false)
            if (!supported) log("stop: pidfd probe failed")
            supported
        }
    }

    private class Unsupported(val errno: Int) : Exception()

    private fun fallback(builder: ProcessBuilder, requireAuthenticated: Boolean): Process {
        check(!requireAuthenticated) { "authenticated Guest launcher is unavailable" }
        return builder.start()
    }

    fun start(builder: ProcessBuilder, requireAuthenticated: Boolean = false): Process =
        startInternal(builder, requireAuthenticated, null)

    /**
     * Offline experiment only. I proves a limited rule set was installed; it
     * does not establish safe hardware isolation or authorize Guest callers.
     * Do not use this entry point to enable the product vibration bridge.
     */
    internal fun startLimitedVibrationExperiment(builder: ProcessBuilder, guestRoot: File): ManagedGuestProcess {
        require(guestRoot.isAbsolute) { "Guest root must be absolute" }
        val result = startInternal(builder, true, guestRoot)
        check(result is ManagedGuestProcess) { "authenticated Guest launcher is unavailable" }
        return result
    }

    private fun startInternal(builder: ProcessBuilder, requireAuthenticated: Boolean, limitedRoot: File?): Process {
        if (!available) return fallback(builder, requireAuthenticated)
        val original = builder.command().toList()
        require(original.isNotEmpty()) { "Guest command is empty" }
        val name = "rme-" + UUID.randomUUID().toString().replace("-", "")
        val server = LocalServerSocket(name)
        var child: Process? = null
        var handle = -1
        var verified = false
        try {
            val launcher = listOf(selectGuestLauncher(nativeDir, limitedRoot != null).absolutePath, name)
            builder.command(if (limitedRoot == null) launcher + original
                else launcher + listOf("--experimental-vibration-guard", limitedRoot.absolutePath) + original)
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
                commitLauncherProtocol(peer.outputStream, peer.inputStream, limitedRoot != null)
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
                log("stop: guest pidfd unavailable (errno=${failure.errno})")
                builder.command(original)
                return fallback(builder, requireAuthenticated)
            }
            throw failure
        } finally {
            if (handle >= 0) HostNative.pidfdClose(handle)
            server.close()
        }
    }
}
