package app.aemu.core

import java.util.concurrent.ArrayBlockingQueue
import java.util.concurrent.CountDownLatch
import java.util.concurrent.RejectedExecutionException
import java.util.concurrent.Semaphore
import java.util.concurrent.ThreadPoolExecutor
import java.util.concurrent.TimeUnit

/** Bounds waiting even when the host resolver blocks before returning a cancel handle. */
internal object DnsRawAwaiter {
    private val workers = ThreadPoolExecutor(32, 32, 30, TimeUnit.SECONDS,
        ArrayBlockingQueue<Runnable>(32), { job ->
            Thread(job, "rme-raw-dns").apply { isDaemon = true }
        }).apply { allowCoreThreadTimeOut(true) }

    fun await(slots: Semaphore, timeoutMs: Long,
              start: ((DnsRawProtocol.Result) -> Unit) -> (() -> Unit)): DnsRawProtocol.Result {
        if (!slots.tryAcquire()) return DnsRawProtocol.Result.Error(16 /* EBUSY */)
        val lock = Any()
        val done = CountDownLatch(1)
        var started = false
        var complete = false
        var expired = false
        var released = false
        var answer: DnsRawProtocol.Result? = null
        var failure: Throwable? = null
        var cancelAction: (() -> Unit)? = null

        fun releaseLocked() {
            if (!released) { released = true; slots.release() }
        }
        fun cancelAndRelease(cancel: () -> Unit) {
            try {
                cancel()
                synchronized(lock) { releaseLocked() }
            } catch (t: Throwable) {
                // Keep the permit if cancellation is uncertain; never report success.
                System.err.println("dns: raw query cancellation failed: " + t.javaClass.name)
            }
        }
        try {
            workers.execute {
                try {
                    val cancel = start { result ->
                        synchronized(lock) {
                            if (!expired && !complete) {
                                answer = result
                                complete = true
                                done.countDown()
                                if (started) releaseLocked()
                            }
                        }
                    }
                    val cancelNow = synchronized(lock) {
                        started = true
                        cancelAction = cancel
                        if (complete) releaseLocked()
                        expired
                    }
                    if (cancelNow) cancelAndRelease(cancel)
                } catch (t: Throwable) {
                    synchronized(lock) {
                        started = true
                        if (!expired && !complete) {
                            failure = t
                            complete = true
                            done.countDown()
                        }
                        releaseLocked()
                    }
                }
            }
        } catch (_: RejectedExecutionException) {
            synchronized(lock) { releaseLocked() }
            return DnsRawProtocol.Result.Error(16 /* EBUSY */)
        }

        var interrupted = false
        val signaled = try { done.await(timeoutMs, TimeUnit.MILLISECONDS) }
        catch (_: InterruptedException) { interrupted = true; false }
        if (interrupted) Thread.currentThread().interrupt()
        var cancel: (() -> Unit)? = null
        synchronized(lock) {
            if (!signaled && !complete) {
                expired = true
                if (started) cancel = cancelAction
            } else if (complete) {
                failure?.let { throw it }
                return answer ?: DnsRawProtocol.Result.Error(5 /* EIO */)
            }
        }
        // A completed start is canceled in a worker so a blocked cancel cannot
        // extend this caller's deadline. A blocked start cancels when it returns.
        cancel?.let { action ->
            try { workers.execute { cancelAndRelease(action) } }
            catch (_: RejectedExecutionException) {
                // Keep the permit while cancellation is uncertain.
                System.err.println("dns: raw query cancellation queue full")
            }
        }
        return DnsRawProtocol.Result.Error(if (interrupted) 4 /* EINTR */ else 110 /* ETIMEDOUT */)
    }
}
