package app.aemu.core
import org.junit.Assert.*
import org.junit.Test
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger

class WifiDnsAvailabilityNotifierTest {
    @Test fun rejectedUntilGuestReadyThenRetriesAndDeliversChanges() {
        val calls=AtomicInteger(); val receivedFalse=CountDownLatch(1); val receivedTrue=CountDownLatch(1)
        val notifier=WifiDnsAvailabilityNotifier(10) { value ->
            if (calls.incrementAndGet() < 2) false else {
                if (value) receivedTrue.countDown() else receivedFalse.countDown()
                true
            }
        }
        try {
            notifier.update(false); assertTrue(receivedFalse.await(3,TimeUnit.SECONDS))
            notifier.update(true); assertTrue(receivedTrue.await(3,TimeUnit.SECONDS))
        } finally { notifier.close() }
        val after=calls.get(); notifier.update(false); Thread.sleep(50); assertEquals(after,calls.get())
    }

    @Test fun rapidChangesCoalesceToLatestValueAndStopWaitsForWorker() {
        val entered=CountDownLatch(1); val release=CountDownLatch(1); val latest=CountDownLatch(1)
        val notifier=WifiDnsAvailabilityNotifier(10) { value ->
            if (!value) { entered.countDown(); release.await(2,TimeUnit.SECONDS) } else latest.countDown()
            true
        }
        try {
            notifier.update(false); assertTrue(entered.await(3,TimeUnit.SECONDS))
            notifier.update(true); release.countDown(); assertTrue(latest.await(3,TimeUnit.SECONDS))
        } finally { release.countDown(); notifier.close() }
    }
    @Test fun switchingBetweenTwoValidatedNetworksTriggersAnotherReevaluation() {
        val first=CountDownLatch(1); val second=CountDownLatch(1); val calls=AtomicInteger()
        val notifier=WifiDnsAvailabilityNotifier(10) { value ->
            assertTrue(value)
            if (calls.incrementAndGet() == 1) first.countDown() else second.countDown()
            true
        }
        try {
            notifier.update(true); assertTrue(first.await(3,TimeUnit.SECONDS))
            notifier.update(true, force=true)
            assertTrue(second.await(3,TimeUnit.SECONDS))
        } finally { notifier.close() }
    }
}
