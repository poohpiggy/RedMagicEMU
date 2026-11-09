package app.aemu.core

import java.io.ByteArrayOutputStream
import java.io.InputStream
import java.nio.ByteBuffer
import java.nio.charset.CodingErrorAction
import java.nio.charset.StandardCharsets
import java.util.concurrent.TimeUnit

/**
 * Captures a single Process with bounded output. The supplied process must merge
 * stderr into stdout. A timeout is sticky, even if the process exits zero later.
 * timeoutMs bounds the process wait only: start(), stream access and termination
 * can themselves block. The enclosing lifecycle must stay closed if they do.
 * An uncertain result is not evidence that Guest/provider work has stopped;
 * callers must not reopen access or retry on that basis. In particular a daemon
 * reader may remain blocked when another process holds its pipe open.
 */
internal class BoundedGuestCommand(
    private val outputLimitBytes: Int = 65_536,
    private val readerWaitMs: Long = 2_000,
    private val terminateWaitMs: Long = 2_000,
) {
    enum class Reason { START_FAILURE, TIMEOUT, READER_PENDING, READ_FAILURE, OUTPUT_TRUNCATED, INVALID_UTF8, INTERRUPTED, WAIT_FAILURE, EXIT_UNAVAILABLE }
    sealed class Result {
        data class Completed(val exitCode: Int, val output: String) : Result()
        /** Exit and reader flags are snapshots, not proof that a process tree is quiescent. */
        data class Uncertain(
            val reason: Reason,
            val processExitObserved: Boolean,
            val readerFinished: Boolean,
            val eofObserved: Boolean,
            val terminationRequested: Boolean,
        ) : Result()
    }

    init {
        require(outputLimitBytes in 1..262_144)
        require(readerWaitMs in 1..10_000 && terminateWaitMs in 1..10_000)
    }

    private class Capture(private val stream: InputStream, private val limit: Int) {
        private val data = ByteArrayOutputStream()
        @Volatile var eof = false
            private set
        @Volatile var failed = false
            private set
        @Volatile var truncated = false
            private set

        val thread = Thread({
            try {
                val buffer = ByteArray(8192)
                while (true) {
                    val count = stream.read(buffer)
                    if (count < 0) { eof = true; break }
                    if (count == 0) { failed = true; break }
                    val remaining = limit - data.size()
                    val keep = minOf(count, remaining)
                    if (keep > 0) data.write(buffer, 0, keep)
                    if (keep < count) truncated = true
                }
            } catch (_: Exception) {
                failed = true
            } finally {
                try { stream.close() } catch (_: Exception) { failed = true }
            }
        }, "rme-guest-command-reader").apply { isDaemon = true }

        /** Only read after the worker has ended. */
        fun bytes(): ByteArray = data.toByteArray()
    }

    private fun requestTermination(process: Process): Boolean = try {
        process.destroyForcibly(); true
    } catch (_: Exception) { false }

    fun run(timeoutMs: Long, start: () -> Process): Result {
        require(timeoutMs > 0)
        val process = try { start() } catch (_: InterruptedException) {
            Thread.currentThread().interrupt()
            return Result.Uncertain(Reason.INTERRUPTED, false, true, false, false)
        } catch (_: Exception) {
            return Result.Uncertain(Reason.START_FAILURE, false, true, false, false)
        }
        val capture = try { Capture(process.inputStream, outputLimitBytes) } catch (_: Exception) {
            val requested = requestTermination(process)
            return Result.Uncertain(Reason.READ_FAILURE, false, true, false, requested)
        }
        try { capture.thread.start() } catch (_: Exception) {
            val requested = requestTermination(process)
            return Result.Uncertain(Reason.READ_FAILURE, false, true, false, requested)
        }

        val exited = try { process.waitFor(timeoutMs, TimeUnit.MILLISECONDS) }
        catch (_: InterruptedException) {
            val requested = requestTermination(process)
            Thread.currentThread().interrupt()
            return Result.Uncertain(Reason.INTERRUPTED, false, !capture.thread.isAlive, capture.eof, requested)
        } catch (_: Exception) {
            val requested = requestTermination(process)
            return Result.Uncertain(Reason.WAIT_FAILURE, false, !capture.thread.isAlive, capture.eof, requested)
        }

        if (!exited) {
            val requested = requestTermination(process)
            val afterTermination = try { process.waitFor(terminateWaitMs, TimeUnit.MILLISECONDS) }
            catch (_: InterruptedException) {
                Thread.currentThread().interrupt()
                return Result.Uncertain(Reason.TIMEOUT, false, !capture.thread.isAlive, capture.eof, requested)
            } catch (_: Exception) { false }
            try { capture.thread.join(readerWaitMs) }
            catch (_: InterruptedException) { Thread.currentThread().interrupt() }
            return Result.Uncertain(Reason.TIMEOUT, afterTermination, !capture.thread.isAlive, capture.eof, requested)
        }

        val code = try { process.exitValue() } catch (_: Exception) {
            return Result.Uncertain(Reason.EXIT_UNAVAILABLE, true, !capture.thread.isAlive, capture.eof, false)
        }
        try { capture.thread.join(readerWaitMs) } catch (_: InterruptedException) {
            Thread.currentThread().interrupt()
            return Result.Uncertain(Reason.INTERRUPTED, true, !capture.thread.isAlive, capture.eof, false)
        }
        if (capture.thread.isAlive) return Result.Uncertain(Reason.READER_PENDING, true, false, capture.eof, false)
        if (capture.failed || !capture.eof) return Result.Uncertain(Reason.READ_FAILURE, true, true, capture.eof, false)
        if (capture.truncated) return Result.Uncertain(Reason.OUTPUT_TRUNCATED, true, true, true, false)
        val output = try {
            StandardCharsets.UTF_8.newDecoder().onMalformedInput(CodingErrorAction.REPORT)
                .onUnmappableCharacter(CodingErrorAction.REPORT).decode(ByteBuffer.wrap(capture.bytes())).toString()
        } catch (_: Exception) {
            return Result.Uncertain(Reason.INVALID_UTF8, true, true, true, false)
        }
        return Result.Completed(code, output)
    }
}
