package app.aemu.core

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.util.concurrent.Semaphore
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.ConcurrentLinkedQueue

class DnsRawAwaiterTest {
    private fun awaitPermit(slots: Semaphore) {
        assertTrue("permit was not released", slots.tryAcquire(1, TimeUnit.SECONDS))
        slots.release()
    }

    @Test fun blockedStartCannotExtendCallerDeadlineOrReleasePermitEarly() {
        val slots = Semaphore(1)
        val entered = CountDownLatch(1)
        val unblock = CountDownLatch(1)
        val canceled = CountDownLatch(1)
        val returned = CountDownLatch(1)
        var result: DnsRawProtocol.Result? = null
        val caller = Thread {
            result = DnsRawAwaiter.await(slots, 50) { _ ->
                entered.countDown(); unblock.await();
                { canceled.countDown(); Unit }
            }
            returned.countDown()
        }
        caller.isDaemon = true
        caller.start()
        try {
            assertTrue(entered.await(1, TimeUnit.SECONDS))
            assertTrue("start blocked the deadline", returned.await(1, TimeUnit.SECONDS))
            assertEquals(DnsRawProtocol.Result.Error(110), result)
            assertEquals("blocked startup must retain its permit", 0, slots.availablePermits())
        } finally { unblock.countDown() }
        assertTrue(canceled.await(1, TimeUnit.SECONDS))
        awaitPermit(slots)
    }

    @Test fun blockedCancelDoesNotExtendCallerDeadlineOrReleasePermitEarly() {
        val slots = Semaphore(1)
        val cancelEntered = CountDownLatch(1)
        val unblock = CountDownLatch(1)
        val returned = CountDownLatch(1)
        var result: DnsRawProtocol.Result? = null
        val caller = Thread {
            result = DnsRawAwaiter.await(slots, 50) { _ ->
                { cancelEntered.countDown(); unblock.await(); Unit }
            }
            returned.countDown()
        }
        caller.isDaemon = true
        caller.start()
        try {
            assertTrue(returned.await(1, TimeUnit.SECONDS))
            assertTrue(cancelEntered.await(1, TimeUnit.SECONDS))
            assertEquals(DnsRawProtocol.Result.Error(110), result)
            assertEquals(0, slots.availablePermits())
        } finally { unblock.countDown() }
        awaitPermit(slots)
    }

    @Test fun callbackAfterTimeoutCannotConvertFailureToSuccess() {
        val slots = Semaphore(1)
        val canceled = CountDownLatch(1)
        var complete: ((DnsRawProtocol.Result) -> Unit)? = null
        val result = DnsRawAwaiter.await(slots, 50) { callback ->
            complete = callback;
            { canceled.countDown(); Unit }
        }
        assertEquals(DnsRawProtocol.Result.Error(110), result)
        assertTrue(canceled.await(1, TimeUnit.SECONDS))
        complete!!(DnsRawProtocol.Result.Answer(0, byteArrayOf(1, 2)))
        awaitPermit(slots)
        assertEquals(DnsRawProtocol.Result.Error(110), result)
    }

    @Test fun blockedStartsUseBoundedPermitsAndRejectFurtherRequests() {
        val slots = Semaphore(32)
        val started = CountDownLatch(32)
        val unblock = CountDownLatch(1)
        val canceled = CountDownLatch(32)
        val returned = CountDownLatch(32)
        val results = ConcurrentLinkedQueue<DnsRawProtocol.Result>()
        try {
            repeat(32) {
                Thread {
                    results.add(DnsRawAwaiter.await(slots, 200) { _ ->
                        started.countDown(); unblock.await();
                        { canceled.countDown(); Unit }
                    })
                    returned.countDown()
                }.apply { isDaemon = true; start() }
            }
            assertTrue(started.await(2, TimeUnit.SECONDS))
            assertTrue(returned.await(2, TimeUnit.SECONDS))
            assertEquals(32, results.size)
            assertTrue(results.all { it == DnsRawProtocol.Result.Error(110) })
            assertEquals(0, slots.availablePermits())
            assertEquals(DnsRawProtocol.Result.Error(16), DnsRawAwaiter.await(slots, 100) { _ ->
                throw AssertionError("query started beyond limit")
            })
        } finally { unblock.countDown() }
        assertTrue(canceled.await(2, TimeUnit.SECONDS))
        assertTrue(slots.tryAcquire(32, 2, TimeUnit.SECONDS))
        slots.release(32)
    }
    @Test fun answerIsDeliveredWithoutCancellation() {
        val cancelCount = AtomicInteger()
        val slots = Semaphore(1)
        val result = DnsRawAwaiter.await(slots, 1000) { complete ->
            complete(DnsRawProtocol.Result.Answer(0, byteArrayOf(1, 2)));
            { cancelCount.incrementAndGet(); Unit }
        } as DnsRawProtocol.Result.Answer
        assertArrayEquals(byteArrayOf(1, 2), result.packet)
        assertEquals(0, cancelCount.get())
        awaitPermit(slots)
    }

    @Test fun timeoutCancelsAndReleasesSlot() {
        val cancelCount = AtomicInteger()
        val canceled = CountDownLatch(1)
        val slots = Semaphore(1)
        val result = DnsRawAwaiter.await(slots, 10) { _ -> { cancelCount.incrementAndGet(); canceled.countDown(); Unit } }
        assertEquals(DnsRawProtocol.Result.Error(110), result)
        assertTrue(canceled.await(1, TimeUnit.SECONDS))
        assertEquals(1, cancelCount.get())
        awaitPermit(slots)
    }

    @Test fun busyDoesNotStartAndStartFailureReleasesSlot() {
        val busy = Semaphore(0)
        assertEquals(DnsRawProtocol.Result.Error(16), DnsRawAwaiter.await(busy, 1) { _ ->
            throw AssertionError("started without a permit")
        })
        val slots = Semaphore(1)
        try {
            DnsRawAwaiter.await(slots, 1000) { _ -> throw IllegalStateException("start failed") }
            throw AssertionError("start failure was lost")
        } catch (_: IllegalStateException) { }
        awaitPermit(slots)
    }

    @Test fun interruptionCancelsAndPreservesInterruptFlag() {
        val slots = Semaphore(1)
        val canceled = AtomicInteger()
        val cancelDone = CountDownLatch(1)
        val worker = Thread {
            Thread.currentThread().interrupt()
            val result = DnsRawAwaiter.await(slots, 100) { _ -> { canceled.incrementAndGet(); cancelDone.countDown(); Unit } }
            assertEquals(DnsRawProtocol.Result.Error(4), result)
            assertTrue(Thread.currentThread().isInterrupted)
        }
        val failure = arrayOfNulls<Throwable>(1)
        worker.uncaughtExceptionHandler = Thread.UncaughtExceptionHandler { _, t -> failure[0] = t }
        worker.start(); worker.join(1000)
        assertTrue(!worker.isAlive)
        assertEquals(null, failure[0])
        assertTrue(cancelDone.await(1, TimeUnit.SECONDS))
        assertEquals(1, canceled.get())
        awaitPermit(slots)
    }
}
