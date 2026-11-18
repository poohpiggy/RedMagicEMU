package app.aemu.core

import java.io.InputStream
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import kotlin.concurrent.thread

/** Owns one host process. A ready host alone does not provide Guest Vulkan. */
internal class HostVulkanService(
    private val spawnStop: (() -> Unit) -> Unit = { block -> thread(name = "vulkan-host-stop", isDaemon = true, block = block); Unit },
    private val starter: (ProcessBuilder) -> Process = { it.start() }
) {
    data class Result(val ok: Boolean, val message: String)

    private class Attempt(val shutdownGraceMs: Long) {
        var process: Process? = null
        var ready = false
        var cancelled = false
        var terminationStarted = false
        var forced = false
        var finished = false
        var exitCode: Int? = null
        var failure: String? = null
        var stderrDetail: String? = null
        val stderrDone = CountDownLatch(1)
    }

    private val lock = java.lang.Object()
    private var current: Attempt? = null
    private var error: String? = null

    /** Builder is exclusively owned by this call until the asynchronous launch returns. */
    fun start(builder: ProcessBuilder, timeoutMs: Long, shutdownGraceMs: Long = 250): Result {
        if (timeoutMs !in 1..60000) return Result(false, "invalid startup timeout")
        if (builder.redirectErrorStream() || builder.redirectInput() != ProcessBuilder.Redirect.PIPE ||
            builder.redirectOutput() != ProcessBuilder.Redirect.PIPE || builder.redirectError() != ProcessBuilder.Redirect.PIPE)
            return Result(false, "host requires separate stdin, stdout and stderr pipes")
        val attempt = synchronized(lock) {
            if (current != null) return Result(false, "service already active or stopping")
            Attempt(shutdownGraceMs).also { current = it; error = null }
        }
        try {
            thread(name = "vulkan-host-launch", isDaemon = true) { launch(attempt, builder) }
        } catch (t: Throwable) {
            synchronized(lock) { attempt.failure = "launch thread failed: ${t.javaClass.simpleName}"; error = attempt.failure; attempt.finished = true; current = null; lock.notifyAll() }
            return Result(false, attempt.failure!!)
        }
        val deadline = System.nanoTime() + TimeUnit.MILLISECONDS.toNanos(timeoutMs)
        synchronized(lock) {
            while (!attempt.ready && !attempt.finished && !attempt.cancelled && attempt.failure == null) {
                val left = deadline - System.nanoTime()
                if (left <= 0) break
                try { timedWait(left) } catch (_: InterruptedException) {
                    Thread.currentThread().interrupt()
                    fail(attempt, "startup interrupted")
                    cancel(attempt)
                    return Result(false, "startup interrupted")
                }
            }
            if (attempt.ready && !attempt.cancelled && !attempt.finished && attempt.process?.isAlive == true)
                return Result(true, "host ready")
            val reason = attempt.failure ?: if (attempt.cancelled) "startup cancelled" else if (attempt.finished) "host exited before ready" else "startup timeout"
            fail(attempt, reason)
            if (!attempt.finished) cancel(attempt)
            // A child can exit and close stdout before its bounded stderr reader
            // receives the native failure line. Never wait beyond start's deadline.
            while (attempt.process != null && attempt.stderrDetail == null && attempt.stderrDone.count != 0L) {
                val left = deadline - System.nanoTime()
                if (left <= 0) break
                try { timedWait(left) } catch (_: InterruptedException) { Thread.currentThread().interrupt(); break }
            }
            return Result(false, describe(attempt, reason))
        }
    }

    /** Waits at most timeoutMs. A forced exit or an unconfirmed exit is reported as failure. */
    fun stop(timeoutMs: Long): Result {
        if (timeoutMs !in 1..60000) return Result(false, "invalid stop timeout")
        val deadline = System.nanoTime() + TimeUnit.MILLISECONDS.toNanos(timeoutMs)
        synchronized(lock) {
            val attempt = current ?: return Result(true, "already stopped")
            cancel(attempt)
            while (!attempt.finished) {
                if (!attempt.terminationStarted) return Result(false, attempt.failure ?: "host stop failed")
                val left = deadline - System.nanoTime()
                if (left <= 0) { fail(attempt, "host termination not confirmed"); return Result(false, "host termination not confirmed") }
                try { timedWait(left) } catch (_: InterruptedException) {
                    Thread.currentThread().interrupt()
                    return Result(false, "stop interrupted; host termination not confirmed")
                }
            }
            if (attempt.forced) return Result(false, "host required forced termination")
            if (attempt.exitCode != null && attempt.exitCode != 0) return Result(false, "host exit code ${attempt.exitCode}")
            return Result(true, "host stopped")
        }
    }

    fun isRunning(): Boolean = synchronized(lock) {
        current?.let { it.ready && !it.cancelled && !it.finished && it.process?.isAlive == true } == true
    }

    fun lastFailure(): String? = synchronized(lock) { error }

    private fun timedWait(nanos: Long) {
        val millis = TimeUnit.NANOSECONDS.toMillis(nanos)
        lock.wait(millis, (nanos - TimeUnit.MILLISECONDS.toNanos(millis)).toInt())
    }

    // Called only with lock held. A stale reader cannot overwrite a newer attempt.
    private fun fail(attempt: Attempt, message: String) {
        if (attempt.failure == null) attempt.failure = message
        if (current === attempt) error = describe(attempt, attempt.failure!!)
        lock.notifyAll()
    }

    private fun describe(attempt: Attempt, message: String): String =
        attempt.stderrDetail?.let { "$message: $it" } ?: message

    private fun failStop(attempt: Attempt, message: String) {
        attempt.failure = attempt.failure?.let { "${it.take(256)}; $message" } ?: message
        if (current === attempt) error = describe(attempt, attempt.failure!!)
        lock.notifyAll()
    }

    private fun launch(attempt: Attempt, builder: ProcessBuilder) {
        val process = try { starter(builder) } catch (t: Throwable) {
            synchronized(lock) {
                fail(attempt, "host launch failed: ${t.javaClass.simpleName}")
                attempt.finished = true
                attempt.stderrDone.countDown()
                if (current === attempt) current = null
                lock.notifyAll()
            }
            return
        }
        synchronized(lock) { attempt.process = process; lock.notifyAll() }
        try {
            thread(name = "vulkan-host-stdout", isDaemon = true) { readOutput(attempt, process) }
            thread(name = "vulkan-host-stderr", isDaemon = true) {
                try { readErrors(attempt, process.errorStream) } finally {
                    attempt.stderrDone.countDown()
                    synchronized(lock) { lock.notifyAll() }
                }
            }
        } catch (t: Throwable) {
            attempt.stderrDone.countDown()
            synchronized(lock) { fail(attempt, "host output reader failed: ${t.javaClass.simpleName}"); cancel(attempt) }
        }
        var interrupted = false
        var code: Int
        while (true) {
            try { code = process.waitFor(); break } catch (_: InterruptedException) {
                interrupted = true
                synchronized(lock) { fail(attempt, "host wait interrupted"); cancel(attempt) }
            }
        }
        try { attempt.stderrDone.await(250, TimeUnit.MILLISECONDS) } catch (_: InterruptedException) { interrupted = true }
        synchronized(lock) {
            attempt.exitCode = code
            if (!attempt.cancelled) fail(attempt, "host exited ${if (attempt.ready) "unexpectedly" else "before ready"} (code $code)")
            attempt.finished = true
            if (current === attempt) current = null
            lock.notifyAll()
        }
        if (interrupted) Thread.currentThread().interrupt()
    }

    private fun readOutput(attempt: Attempt, process: Process) {
        try {
            val input = process.inputStream
            val first = ByteArray(129)
            var count = 0
            var b: Int
            do {
                b = input.read()
                if (b < 0 || b == 10) break
                if (count == first.size) break
                first[count++] = b.toByte()
            } while (true)
            if (count > 0 && first[count - 1] == 13.toByte()) count--
            val good = b == 10 && String(first, 0, count, Charsets.US_ASCII) == "RME_VK_HOST_READY"
            synchronized(lock) {
                if (!attempt.cancelled && !attempt.finished) {
                    if (good && process.isAlive) attempt.ready = true
                    else { fail(attempt, "host did not send valid readiness"); cancel(attempt) }
                    lock.notifyAll()
                }
            }
            drain(input)
        } catch (t: Throwable) {
            synchronized(lock) {
                if (!attempt.cancelled && !attempt.finished) { fail(attempt, "host output failed: ${t.javaClass.simpleName}"); cancel(attempt) }
            }
        }
    }

    private fun drain(input: InputStream) {
        try { input.use { val bytes = ByteArray(4096); while (it.read(bytes) >= 0) {} } } catch (_: Exception) { }
    }

    private fun readErrors(attempt: Attempt, input: InputStream) {
        try {
            input.use { stream ->
                val bytes = ByteArray(4096)
                val line = StringBuilder()
                fun record() {
                    val detail = line.toString().trimEnd()
                    if (detail.startsWith("RME_VK_HOST_")) synchronized(lock) {
                        if (attempt.stderrDetail == null) attempt.stderrDetail = detail
                        if (attempt.failure != null && current === attempt) error = describe(attempt, attempt.failure!!)
                        lock.notifyAll()
                    }
                    line.setLength(0)
                }
                while (true) {
                    val n = stream.read(bytes)
                    if (n < 0) break
                    for (i in 0 until n) {
                        val b = bytes[i].toInt() and 255
                        if (b == 10) record()
                        else if (line.length < 256) line.append(if (b in 32..126) b.toChar() else '?')
                    }
                }
                if (line.isNotEmpty()) record()
            }
        } catch (_: Exception) { }
    }

    // Called only with lock held; termination work happens off the caller's thread.
    private fun cancel(attempt: Attempt) {
        attempt.cancelled = true
        lock.notifyAll()
        if (attempt.terminationStarted) return
        attempt.terminationStarted = true
        try {
            spawnStop {
                try {
                    val process = synchronized(lock) {
                        while (attempt.process == null && !attempt.finished) lock.wait()
                        attempt.process
                    }
                    if (process != null) {
                        try { process.outputStream.use { it.write('Q'.code); it.flush() } } catch (_: Exception) { }
                        if (!process.waitFor(attempt.shutdownGraceMs, TimeUnit.MILLISECONDS)) {
                            synchronized(lock) { attempt.forced = true; fail(attempt, "host required forced termination") }
                            process.destroy()
                            if (!process.waitFor(250, TimeUnit.MILLISECONDS)) process.destroyForcibly()
                        }
                        if (!process.waitFor(2, TimeUnit.SECONDS)) synchronized(lock) {
                            attempt.terminationStarted = false
                            fail(attempt, "host termination not confirmed")
                        }
                    }
                } catch (t: Throwable) {
                    synchronized(lock) {
                        attempt.terminationStarted = false
                        failStop(attempt, "host stop failed: ${t.javaClass.simpleName}")
                    }
                    if (t is InterruptedException) Thread.currentThread().interrupt()
                }
            }
        } catch (t: Throwable) {
            attempt.terminationStarted = false
            failStop(attempt, "host stop thread failed: ${t.javaClass.simpleName}")
        }
    }
}
