package app.aemu.core

import java.io.InputStream
import java.io.OutputStream
import java.util.concurrent.TimeUnit

/** Experimental Process adapter. No production launcher creates this object. */
internal class SupervisedGuestProcess(
    private val stdout: InputStream,
    private val stderr: InputStream,
    private val stdin: OutputStream,
    private val sendSignal: (Int) -> Unit,
) : Process() {
    private val monitor = java.lang.Object()
    private var code: Int? = null
    private var controlLost = false

    override fun getInputStream(): InputStream = stdout
    override fun getErrorStream(): InputStream = stderr
    override fun getOutputStream(): OutputStream = stdin

    /** Only an authenticated session's exit frame may establish an exit. */
    internal fun onExitFrame(exit: Int) = synchronized(monitor) {
        require(exit in 0..255) { "Invalid supervised exit code" }
        check(!controlLost) { "Supervisor session was lost" }
        val previous = code
        check(previous == null || previous == exit) { "Conflicting supervisor exit frames" }
        code = exit
        monitor.notifyAll()
    }

    internal fun onControlLost() = synchronized(monitor) {
        if (code == null) controlLost = true
        monitor.notifyAll()
    }

    override fun waitFor(): Int = synchronized(monitor) {
        while (code == null && !controlLost) monitor.wait()
        check(!controlLost) { "Supervised process status is unknown" }
        checkNotNull(code)
    }

    override fun waitFor(timeout: Long, unit: TimeUnit): Boolean = synchronized(monitor) {
        val maximum = unit.toNanos(timeout).coerceAtLeast(0)
        val started = System.nanoTime()
        var remaining = maximum
        while (code == null && !controlLost && remaining > 0) {
            TimeUnit.NANOSECONDS.timedWait(monitor, remaining)
            val elapsed = System.nanoTime() - started
            remaining = if (elapsed >= maximum) 0 else maximum - elapsed
        }
        check(!controlLost) { "Supervised process status is unknown" }
        code != null
    }

    override fun exitValue(): Int = synchronized(monitor) {
        check(!controlLost) { "Supervised process status is unknown" }
        code ?: throw IllegalThreadStateException("Supervised process is still running")
    }

    // A lost control channel is not proof of exit; keep the VM cleanup conservative.
    override fun isAlive(): Boolean = synchronized(monitor) { code == null }

    private fun signal(value: Int) {
        val shouldSend = synchronized(monitor) {
            check(!controlLost) { "Supervisor session was lost" }
            code == null
        }
        // The supervisor must bind this operation to the session's pidfd: exit
        // can race the send, and no PID-only signal is safe in that window.
        if (shouldSend) sendSignal(value)
    }

    override fun destroy() = signal(15)
    override fun destroyForcibly(): Process { signal(9); return this }
}
