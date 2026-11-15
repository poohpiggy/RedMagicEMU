package app.aemu.core
import org.junit.Assert.*
import org.junit.Test

class DnsStopCleanupTest {
    @Test fun dnsCloseFailureStillRunsRadioVibrationAdbAndGuestCleanup() {
        val lifecycle=VmLifecycle();assertTrue(lifecycle.begin());lifecycle.endBoot()
        val expected=IllegalStateException("dns close failed")
        var dnsFailure: Throwable?=null
        val completed=mutableListOf<String>()
        try {
            lifecycle.stop(onRequest={
                dnsFailure=captureDnsStopFailure { throw expected }
                completed += "radio";completed += "vibration";completed += "adb"
            }) {
                completed += "guest"
                dnsFailure?.let { throw it }
            }
            fail("unconfirmed DNS close reported success")
        } catch (failure: IllegalStateException) { assertSame(expected,failure) }
        assertEquals(listOf("radio","vibration","adb","guest"),completed)
        assertFalse("failed cleanup must block new boot",lifecycle.begin())
    }
    @Test fun unconfirmedRetirementBlocksRestartAndRemainsVisible() {
        val guard=TargetDnsRetirementGuard()
        assertTrue(guard.canStart());guard.begin();assertFalse(guard.canStart())
        try { guard.requireComplete();fail("in-flight cleanup accepted") } catch (_: IllegalStateException) { }
        val expected=IllegalStateException("close unknown");guard.failed(expected)
        assertFalse(guard.canStart())
        try { guard.requireComplete();fail("failed cleanup accepted") } catch (failure: IllegalStateException) { assertSame(expected,failure) }
        assertFalse("failure must continue to block another verifier",guard.canStart())
    }
    @Test fun confirmedRetirementAllowsNextVerifier() {
        val guard=TargetDnsRetirementGuard();guard.begin();guard.complete()
        guard.requireComplete();assertTrue(guard.canStart())
    }
    @Test fun pendingCleanupBlocksNewVerifierUntilItCompletes() {
        val guard=TargetDnsRetirementGuard()
        val entered=java.util.concurrent.CountDownLatch(1)
        val release=java.util.concurrent.CountDownLatch(1)
        guard.begin()
        val worker=Thread { entered.countDown();release.await();guard.complete() }
        worker.start();assertTrue(entered.await(1,java.util.concurrent.TimeUnit.SECONDS))
        assertFalse(guard.canStart())
        try { guard.requireComplete();fail("pending cleanup reported complete") } catch (_: IllegalStateException) { }
        release.countDown();worker.join(1000);assertFalse(worker.isAlive);assertTrue(guard.canStart())
    }
    @Test fun dnsVerifierCannotSpawnWithoutItsAuthenticatedChannel() {
        val channel=Any()
        assertSame(channel, dnsChannelForSpawn("redmagic-wifi-dns",channel))
        assertNull(dnsChannelForSpawn("aemu-radio",channel))
        try { dnsChannelForSpawn<Any>("redmagic-wifi-dns",null);fail("missing channel accepted") }
        catch (_: IllegalStateException) { }
    }
    @Test fun skippedVerifierLaunchDoesNotCountAsStarted() {
        val old=Any();val new=Any()
        assertFalse(dnsVerifierWasStarted<Any>(null,null))
        assertFalse(dnsVerifierWasStarted(old,old))
        assertTrue(dnsVerifierWasStarted(old,new))
        assertTrue(dnsVerifierWasStarted(null,new))
    }
}
