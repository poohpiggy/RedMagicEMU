package app.aemu.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit

class FirstRingtoneInitStateTest {
    @Test fun existingInstanceNeverStartsInitialization() {
        val state = FirstRingtoneInitState.existing()
        assertTrue(state.mayAcceptInput)
        assertFalse(state.mayStartWrite)
        assertThrows(IllegalStateException::class.java) { state.beginWrite() }
    }

    @Test fun freshInstanceBlocksInputBeforeAndDuringWrite() {
        val preparing = FirstRingtoneInitState.fresh()
        assertFalse(preparing.mayAcceptInput)
        assertFalse(preparing.mayStartWrite)
        val intent = preparing.beginWrite()
        assertFalse(intent.mayStartWrite)
        assertFalse(intent.mayAcceptInput)
        val writing = intent.writeIntentPersisted()
        assertFalse(writing.mayAcceptInput)
        assertTrue(writing.mayStartWrite)
        assertFalse(writing.writerStarted().mayStartWrite)
    }

    @Test fun verifiedSuccessRequiresDurableCompletionBeforeInput() {
        val verified = FirstRingtoneInitState.fresh().beginWrite().writeIntentPersisted().writerStarted().writeVerified()
        assertFalse(verified.mayAcceptInput)
        val done = verified.successPersisted(FirstRingtoneInitState.Terminal.SUCCESS)
        assertTrue(done.mayAcceptInput)
        assertFalse(done.mayStartWrite)
    }

    @Test fun uncertainOrInterruptedWriteCannotReleaseOrRetry() {
        val uncertain = FirstRingtoneInitState.fresh().beginWrite().writeIntentPersisted().writerStarted().writeUncertain()
        assertFalse(uncertain.mayAcceptInput)
        assertFalse(uncertain.mayStartWrite)
        assertThrows(IllegalStateException::class.java) { uncertain.successPersisted(FirstRingtoneInitState.Terminal.SUCCESS) }
        assertThrows(IllegalStateException::class.java) { uncertain.beginWrite() }
        assertFalse(FirstRingtoneInitState.interruptedWhileWriting().mayAcceptInput)
        assertFalse(FirstRingtoneInitState.interruptedWhileWriting().mayStartWrite)
    }

    @Test fun failedPreparationCanReleaseOnlyAfterAbandonmentIsDurable() {
        val abandonPending = FirstRingtoneInitState.fresh().abandonBeforeWrite()
        assertFalse(abandonPending.mayAcceptInput)
        assertFalse(abandonPending.mayStartWrite)
        val abandoned = abandonPending.abandonmentPersisted(FirstRingtoneInitState.Terminal.ABANDONED)
        assertTrue(abandoned.mayAcceptInput)
        assertFalse(abandoned.mayStartWrite)
    }

    @Test fun uncertaintyCanBeAbandonedOnlyAfterWriterQuiescentAndPersisted() {
        val uncertain = FirstRingtoneInitState.fresh().beginWrite().writeIntentPersisted().writerStarted().writeUncertain()
        val abandonPending = uncertain.writerQuiescentAndAbandon()
        assertFalse(abandonPending.mayAcceptInput)
        assertFalse(abandonPending.mayStartWrite)
        assertTrue(abandonPending.abandonmentPersisted(FirstRingtoneInitState.Terminal.ABANDONED).mayAcceptInput)
    }

    @Test fun callbacksCannotSkipTheWriteIntentOrItsClaim() {
        val preparing = FirstRingtoneInitState.fresh()
        assertThrows(IllegalStateException::class.java) { preparing.writeVerified() }
        assertThrows(IllegalStateException::class.java) { preparing.successPersisted(FirstRingtoneInitState.Terminal.SUCCESS) }
        val intent = preparing.beginWrite()
        assertThrows(IllegalStateException::class.java) { intent.writerStarted() }
        assertThrows(IllegalStateException::class.java) { intent.writeVerified() }
        val ready = intent.writeIntentPersisted()
        assertThrows(IllegalStateException::class.java) { ready.writeVerified() }
        assertFalse(ready.mayAcceptInput)
    }

    @Test fun inflightWriterCannotBeAbandonedWithoutQuiescence() {
        val writing = FirstRingtoneInitState.fresh().beginWrite().writeIntentPersisted().writerStarted()
        assertThrows(IllegalStateException::class.java) { writing.abandonBeforeWrite() }
        assertThrows(IllegalStateException::class.java) { writing.writerQuiescentAndAbandon() }
        assertThrows(IllegalStateException::class.java) { writing.successPersisted(FirstRingtoneInitState.Terminal.SUCCESS) }
        assertFalse(writing.mayAcceptInput)
    }

    @Test fun onlyOneConcurrentCallerCanClaimTheWriter() {
        val ready = FirstRingtoneInitState.fresh().beginWrite().writeIntentPersisted()
        val arrived = CountDownLatch(2)
        val go = CountDownLatch(1)
        val executor = Executors.newFixedThreadPool(2)
        try {
            val attempts = (1..2).map {
                executor.submit<Boolean> {
                    arrived.countDown()
                    check(go.await(3, TimeUnit.SECONDS))
                    try { ready.writerStarted(); true } catch (_: IllegalStateException) { false }
                }
            }
            assertTrue(arrived.await(3, TimeUnit.SECONDS))
            go.countDown()
            assertEquals(1, attempts.count { it.get(3, TimeUnit.SECONDS) })
            assertFalse(ready.mayAcceptInput)
            assertFalse(ready.mayStartWrite)
        } finally {
            go.countDown()
            executor.shutdownNow()
        }
    }

    @Test fun intentPersistenceInProgressMustBeQuiescentBeforeAbandonment() {
        val pending = FirstRingtoneInitState.fresh().beginWrite()
        assertThrows(IllegalStateException::class.java) { pending.abandonBeforeWrite() }
        assertFalse(pending.mayAcceptInput)
        pending.intentStoreQuiescentAndAbandon()
        assertFalse(pending.mayAcceptInput)
        pending.abandonmentPersisted(FirstRingtoneInitState.Terminal.ABANDONED)
        assertTrue(pending.mayAcceptInput)
    }

    @Test fun persistedIntentCanBeAbandonedBeforeWriterIsClaimed() {
        val ready = FirstRingtoneInitState.fresh().beginWrite().writeIntentPersisted()
        ready.abandonBeforeWrite()
        assertFalse(ready.mayAcceptInput)
        assertThrows(IllegalStateException::class.java) { ready.writerStarted() }
        ready.abandonmentPersisted(FirstRingtoneInitState.Terminal.ABANDONED)
        assertTrue(ready.mayAcceptInput)
    }

    @Test fun wrongTerminalRecordCannotReleaseInput() {
        val success = FirstRingtoneInitState.fresh().beginWrite().writeIntentPersisted().writerStarted().writeVerified()
        assertThrows(IllegalArgumentException::class.java) { success.successPersisted(FirstRingtoneInitState.Terminal.ABANDONED) }
        assertFalse(success.mayAcceptInput)
        assertThrows(IllegalStateException::class.java) { success.abandonmentPersisted(FirstRingtoneInitState.Terminal.ABANDONED) }
        assertFalse(success.mayAcceptInput)
        val abandoned = FirstRingtoneInitState.fresh().abandonBeforeWrite()
        assertThrows(IllegalArgumentException::class.java) { abandoned.abandonmentPersisted(FirstRingtoneInitState.Terminal.SUCCESS) }
        assertFalse(abandoned.mayAcceptInput)
    }
}
