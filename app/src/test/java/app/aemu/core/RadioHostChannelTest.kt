package app.aemu.core

import org.junit.Assert.*
import org.junit.Test
import java.nio.ByteBuffer
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger
import kotlin.concurrent.thread

class RadioHostChannelTest {
    @Test fun handshakeSequenceCannotBeUsedByAnEvent() {
        assertEquals(Long.MAX_VALUE, RadioHostChannel.HELLO_SEQUENCE)
        RadioHostChannel.frame(17, Long.MAX_VALUE - 1, 2, byteArrayOf(49))
        try {
            RadioHostChannel.frame(17, Long.MAX_VALUE, 2, byteArrayOf(49))
            fail("event reused reserved hello sequence")
        } catch (_: IllegalArgumentException) { }
    }

    @Test fun closeAfterFailureAttemptsAllResourcesOnlyOnce() {
        val closeOnce = RadioChannelCloseOnce()
        val first = java.io.IOException("first resource failed")
        val attempts = mutableListOf<Int>()
        try {
            closeOnce.close {
                RadioHostChannel.closeAll({ attempts += 1; throw first }, { attempts += 2 }, { attempts += 3 })
            }
            fail("close failure hidden")
        } catch (actual: java.io.IOException) { assertSame(first, actual) }
        try {
            closeOnce.close { attempts += 4 }
            fail("prior close failure was reported as success")
        } catch (actual: java.io.IOException) { assertSame(first, actual) }
        assertEquals(listOf(1, 2, 3), attempts)
    }

    @Test fun concurrentCloseReceivesOriginalFailureWithoutRepeatingCleanup() {
        val closeOnce = RadioChannelCloseOnce()
        val failure = java.io.IOException("close failed")
        val entered = CountDownLatch(1)
        val release = CountDownLatch(1)
        val seen = java.util.Collections.synchronizedList(mutableListOf<Throwable>())
        val attempts = AtomicInteger()
        val first = thread {
            try { closeOnce.close { attempts.incrementAndGet(); entered.countDown(); release.await(); throw failure } }
            catch (error: Throwable) { seen.add(error) }
        }
        assertTrue(entered.await(3, TimeUnit.SECONDS))
        val second = thread {
            try { closeOnce.close { attempts.incrementAndGet() } }
            catch (error: Throwable) { seen.add(error) }
        }
        try { assertEquals(1, attempts.get()) } finally {
            release.countDown(); first.join(3000); second.join(3000)
        }
        assertEquals(1, attempts.get())
        assertEquals(2, seen.size)
        assertTrue(seen.all { it === failure })
    }

    @Test fun concurrentStopAndExitCloseOnlyOnce() {
        val closeOnce = RadioChannelCloseOnce()
        val entered = CountDownLatch(1)
        val release = CountDownLatch(1)
        val done = CountDownLatch(2)
        val attempts = AtomicInteger()
        val first = thread {
            try { closeOnce.close { attempts.incrementAndGet(); entered.countDown(); release.await() } }
            finally { done.countDown() }
        }
        assertTrue(entered.await(3, TimeUnit.SECONDS))
        val second = thread {
            try { closeOnce.close { attempts.incrementAndGet() } }
            finally { done.countDown() }
        }
        try {
            assertFalse(done.await(100, TimeUnit.MILLISECONDS))
        } finally {
            release.countDown()
            first.join(3000); second.join(3000)
        }
        assertEquals(0L, done.count)
        assertEquals(1, attempts.get())
    }

    @Test fun framePreservesSessionSequenceAndCallNumber() {
        val frame = RadioHostChannel.frame(17, 1, 2, "+10086".toByteArray(Charsets.US_ASCII))
        val data = ByteBuffer.wrap(frame)
        val magic = ByteArray(4); data.get(magic)
        assertArrayEquals("RME1".toByteArray(Charsets.US_ASCII), magic)
        assertEquals(17, data.long); assertEquals(1, data.long)
        assertEquals(2, data.get().toInt()); assertEquals(6, data.short.toInt())
        val number = ByteArray(6); data.get(number)
        assertEquals("+10086", String(number, Charsets.US_ASCII))
    }

    @Test fun invalidNumbersAndMalformedFramesAreRejected() {
        assertTrue(RadioHostChannel.validNumber("+10086"))
        assertFalse(RadioHostChannel.validNumber("+"))
        assertFalse(RadioHostChannel.validNumber("12;3"))
        assertFalse(RadioHostChannel.validNumber("1".repeat(33)))
        try { RadioHostChannel.frame(0, 1, 1, byteArrayOf(1)); fail() } catch (_: IllegalArgumentException) {}
        try { RadioHostChannel.frame(1, 1, 1, ByteArray(257)); fail() } catch (_: IllegalArgumentException) {}
    }

    @Test fun attachLinkageFailureRetiresWithoutSwallowingFatalErrors() {
        var cleaned = false
        val linked = RadioHostChannel.finishFailedAttach(UnsatisfiedLinkError("missing JNI")) { cleaned = true }
        assertFalse(linked)
        assertTrue(cleaned)
        cleaned = false
        val fatal = OutOfMemoryError("fatal")
        try {
            RadioHostChannel.finishFailedAttach(fatal) { cleaned = true }
            fail("fatal error was swallowed")
        } catch (actual: OutOfMemoryError) { assertSame(fatal, actual) }
        assertTrue(cleaned)
    }

    @Test fun failedCloseStillAttemptsEveryResourceAndPreservesFailures() {
        val attempts = mutableListOf<Int>()
        val first = java.io.IOException("fd close failed")
        val second = java.io.IOException("listener close failed")
        try {
            RadioHostChannel.closeAll(
                { attempts += 1; throw first },
                { attempts += 2 },
                { attempts += 3; throw second },
                { attempts += 4 },
            )
            fail("close failure was hidden")
        } catch (actual: java.io.IOException) {
            assertSame(first, actual)
            assertArrayEquals(arrayOf(second), actual.suppressed)
        }
        assertEquals(listOf(1, 2, 3, 4), attempts)
    }

    @Test fun fatalCloseFailureIsNotHiddenBehindAnEarlierIoFailure() {
        val io = java.io.IOException("first")
        val fatal = OutOfMemoryError("fatal close")
        var lastClosed = false
        try {
            RadioHostChannel.closeAll({ throw io }, { throw fatal }, { lastClosed = true })
            fail("fatal error was swallowed")
        } catch (actual: OutOfMemoryError) {
            assertSame(fatal, actual)
            assertArrayEquals(arrayOf(io), actual.suppressed)
        }
        assertTrue(lastClosed)
    }

    @Test fun fatalAttachFailureSurvivesOrdinaryCleanupFailure() {
        val fatal = OutOfMemoryError("fatal")
        val cleanup = java.io.IOException("cleanup")
        try {
            RadioHostChannel.finishFailedAttach(fatal) { throw cleanup }
            fail("fatal error was swallowed")
        } catch (actual: OutOfMemoryError) {
            assertSame(fatal, actual)
            assertArrayEquals(arrayOf(cleanup), actual.suppressed)
        }
    }

    @Test fun linkageFailureRetiresBeforeAndAfterSending() {
        var cleanups = 0
        val link = UnsatisfiedLinkError("missing JNI")
        assertEquals(RadioSubmitResult.UNAVAILABLE,
            RadioHostChannel.finishFailedSubmit(link, sent = false) { cleanups++ })
        assertEquals(RadioSubmitResult.UNCERTAIN,
            RadioHostChannel.finishFailedSubmit(link, sent = true) { cleanups++ })
        assertEquals(2, cleanups)
        val fatal = OutOfMemoryError("fatal")
        try {
            RadioHostChannel.finishFailedSubmit(fatal, sent = true) { cleanups++ }
            fail("fatal error was swallowed")
        } catch (actual: OutOfMemoryError) { assertSame(fatal, actual) }
        assertEquals(3, cleanups)
    }
}
