package app.aemu.core

import org.junit.Assert.*
import org.junit.Test
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import kotlin.concurrent.thread

class VmLifecycleTest {
    @Test fun stopCannotPassProcessRegistrationAndRejectsLaterStarts() {
        val lifecycle = VmLifecycle()
        assertTrue(lifecycle.begin())
        lifecycle.endBoot()
        val creating = CountDownLatch(1)
        val finishCreate = CountDownLatch(1)
        val registered = CountDownLatch(1)
        val cancelSawRegistration = CountDownLatch(1)
        val creator = thread { lifecycle.create { creating.countDown(); finishCreate.await(); registered.countDown() } }
        assertTrue(creating.await(3, TimeUnit.SECONDS))
        val stopper = thread { lifecycle.stop({ if (registered.count == 0L) cancelSawRegistration.countDown() }) {} }
        try { assertFalse(cancelSawRegistration.await(200, TimeUnit.MILLISECONDS)) }
        finally { finishCreate.countDown(); creator.join(3000); stopper.join(3000) }
        assertEquals(0, cancelSawRegistration.count)
        var started = false
        try { lifecycle.create { started = true } } catch (_: IllegalStateException) {}
        assertFalse(started)
    }
    @Test fun failedCleanupPreventsNewBootUntilCleanupSucceeds() {
        val lifecycle = VmLifecycle()
        assertTrue(lifecycle.begin())
        lifecycle.endBoot()
        try { lifecycle.stop({}) { error("cleanup failed") } } catch (_: IllegalStateException) {}
        assertFalse(lifecycle.begin())
        lifecycle.stop({}) {}
        assertTrue(lifecycle.begin())
        lifecycle.endBoot()
        lifecycle.stop({}) {}
    }
    @Test fun commandIsRejectedAfterStopWasRequested() {
        val lifecycle = VmLifecycle()
        assertTrue(lifecycle.begin())
        val requested = CountDownLatch(1)
        val stopper = thread { lifecycle.stop({ requested.countDown() }) {} }
        assertTrue(requested.await(3, TimeUnit.SECONDS))
        try {
            var ran = false
            try { lifecycle.run { ran = true } } catch (_: IllegalStateException) {}
            assertFalse("command ran after stop was requested", ran)
        } finally { lifecycle.endBoot(); stopper.join(3000) }
    }
    @Test fun stopWaitsForExistingWorkerAndRejectsNewWorker() {
        val lifecycle = VmLifecycle()
        assertTrue(lifecycle.begin())
        lifecycle.endBoot()
        val entered = CountDownLatch(1)
        val release = CountDownLatch(1)
        lifecycle.launch("existing") { entered.countDown(); release.await() }
        assertTrue(entered.await(3, TimeUnit.SECONDS))
        val requested = CountDownLatch(1)
        val stopped = CountDownLatch(1)
        val stopper = thread { lifecycle.stop({ requested.countDown() }) {}; stopped.countDown() }
        assertTrue(requested.await(3, TimeUnit.SECONDS))
        try {
            assertFalse(stopped.await(200, TimeUnit.MILLISECONDS))
            val started = CountDownLatch(1)
            lifecycle.launch("late") { started.countDown() }
            assertFalse(started.await(200, TimeUnit.MILLISECONDS))
        } finally { release.countDown(); stopper.join(3000) }
        assertEquals(0, stopped.count)
    }
    @Test fun stopWaitsForBootAndRejectsLateResourceCreation() {
        val lifecycle = VmLifecycle()
        assertTrue(lifecycle.begin())
        val stopRequested = CountDownLatch(1)
        val stopped = CountDownLatch(1)
        val stop = thread { lifecycle.stop({ stopRequested.countDown() }) { }; stopped.countDown() }
        assertTrue(stopRequested.await(3, TimeUnit.SECONDS))
        try {
            assertFalse("stop returned before boot finished", stopped.await(200, TimeUnit.MILLISECONDS))
            var started = false
            lifecycle.ifRunning { started = true }
            assertFalse("resource started after stop was requested", started)
        } finally { lifecycle.endBoot(); stop.join(3000) }
        assertEquals(0, stopped.count)
    }

    @Test fun duplicateBootAndBootDuringCleanupAreRejected() {
        val lifecycle = VmLifecycle()
        assertTrue(lifecycle.begin())
        assertFalse(lifecycle.begin())
        lifecycle.endBoot()
        val cleanupStarted = CountDownLatch(1)
        val releaseCleanup = CountDownLatch(1)
        val stop = thread { lifecycle.stop({}) { cleanupStarted.countDown(); releaseCleanup.await() } }
        assertTrue(cleanupStarted.await(3, TimeUnit.SECONDS))
        try { assertFalse(lifecycle.begin()) } finally { releaseCleanup.countDown(); stop.join(3000) }
        assertTrue(lifecycle.begin())
        lifecycle.endBoot()
        lifecycle.stop({}) {}
    }
    @Test fun concurrentStopDoesNotReportSuccessAfterCleanupFailure() {
        val lifecycle = VmLifecycle()
        assertTrue(lifecycle.begin())
        lifecycle.endBoot()
        val cleanupStarted = CountDownLatch(1)
        val release = CountDownLatch(1)
        val first = thread {
            try { lifecycle.stop({}) { cleanupStarted.countDown(); release.await(); error("cleanup failed") } }
            catch (_: IllegalStateException) { }
        }
        assertTrue(cleanupStarted.await(3, TimeUnit.SECONDS))
        val secondDone = CountDownLatch(1)
        var secondSucceeded = false
        val second = thread {
            try { lifecycle.stop({}) { error("cleanup still fails") }; secondSucceeded = true }
            catch (_: IllegalStateException) { }
            finally { secondDone.countDown() }
        }
        try {
            val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(3)
            while (second.state != Thread.State.WAITING && System.nanoTime() < deadline) Thread.yield()
            assertEquals(Thread.State.WAITING, second.state)
        } finally { release.countDown(); first.join(3000); second.join(3000) }
        assertEquals(0, secondDone.count)
        assertFalse("stop reported success although cleanup failed", secondSucceeded)
    }

    @Test fun failedStopRequestAllowsLaterCleanupRetry() {
        val lifecycle = VmLifecycle()
        assertTrue(lifecycle.begin())
        lifecycle.endBoot()
        try { lifecycle.stop({ error("request failed") }) {} } catch (_: IllegalStateException) { }
        val done = CountDownLatch(1)
        var cleaned = false
        thread(isDaemon = true) {
            lifecycle.stop({}) { cleaned = true }
            done.countDown()
        }
        assertTrue("later cleanup remained blocked", done.await(1, TimeUnit.SECONDS))
        assertTrue(cleaned)
        assertTrue(lifecycle.begin())
        lifecycle.endBoot()
        lifecycle.stop({}) {}
    }
}
