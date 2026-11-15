package app.aemu.core
import org.junit.Assert.*
import org.junit.Test

class TargetDnsRestartPolicyTest {
    @Test fun failuresRetryWithABoundAndStableRunResetsStreak() {
        val policy=TargetDnsRestartPolicy()
        assertEquals(500L,policy.afterExit(100))
        assertTrue(policy.awaitRetry(0))
        assertEquals(1000L,policy.afterExit(100))
        assertTrue(policy.awaitRetry(0))
        assertEquals(2000L,policy.afterExit(100))
        assertTrue(policy.awaitRetry(0))
        assertEquals(4000L,policy.afterExit(100))
        assertTrue(policy.awaitRetry(0))
        assertEquals(8000L,policy.afterExit(100))
        assertTrue(policy.awaitRetry(0))
        assertEquals(16000L,policy.afterExit(100))
        assertTrue(policy.awaitRetry(0))
        assertEquals(30000L,policy.afterExit(100))
        assertTrue(policy.awaitRetry(0))
        assertEquals(30000L,policy.afterExit(100))
        assertTrue(policy.awaitRetry(0))
        assertEquals(500L,policy.afterExit(60000))
        assertTrue(policy.awaitRetry(0))
        policy.stop()
        assertNull(policy.afterExit(60000))
    }
    @Test fun stopWakesLongBackoffWithoutSchedulingRestart() {
        val policy=TargetDnsRestartPolicy()
        val entered=java.util.concurrent.CountDownLatch(1)
        var allowed=true
        val worker=Thread { entered.countDown(); allowed=policy.awaitRetry(30_000) }
        worker.start(); assertTrue(entered.await(1,java.util.concurrent.TimeUnit.SECONDS))
        policy.stop(); worker.join(1000)
        assertFalse(worker.isAlive); assertFalse(allowed)
    }
    @Test fun staleWatchdogDoesNotRemoveTheReplacementVerifier() {
        val old=Any(); val replacement=Any(); val processes=mutableMapOf("redmagic-wifi-dns" to replacement)
        removeDeadDnsVerifier(processes,old) { false }
        assertSame(replacement,processes["redmagic-wifi-dns"])
        removeDeadDnsVerifier(processes,replacement) { true }
        assertSame(replacement,processes["redmagic-wifi-dns"])
        removeDeadDnsVerifier(processes,replacement) { false }
        assertFalse(processes.containsKey("redmagic-wifi-dns"))
    }
    @Test fun simultaneousFailuresReserveOnlyOneRetry() {
        val policy=TargetDnsRestartPolicy()
        assertEquals(500L,policy.afterExit(100))
        assertNull(policy.afterExit(100))
        assertTrue(policy.awaitRetry(0))
        assertEquals(1000L,policy.afterExit(100))
        policy.stop()
    }
    @Test fun cleanupFailureMustNotScheduleRetry() {
        var retries=0
        try {
            finishDnsRetirement({ throw IllegalStateException("cleanup failed") }, { retries++ })
            fail("cleanup exception was hidden")
        } catch (_: IllegalStateException) { }
        assertEquals(0,retries)
    }
    @Test fun verifierStartupIsSerializedAndExistingChannelWins() {
        val gate=TargetDnsStartGate()
        assertEquals(TargetDnsStartGate.Decision.START,gate.begin(false))
        assertEquals(TargetDnsStartGate.Decision.BUSY,gate.begin(false))
        gate.end()
        assertEquals(TargetDnsStartGate.Decision.ACTIVE,gate.begin(true))
        assertEquals(TargetDnsStartGate.Decision.START,gate.begin(false))
        gate.end()
    }
}
