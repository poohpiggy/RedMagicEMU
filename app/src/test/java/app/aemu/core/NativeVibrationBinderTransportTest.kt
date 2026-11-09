package app.aemu.core

import org.junit.Assert.*
import org.junit.Test
import java.io.IOException
import java.util.concurrent.atomic.AtomicInteger

class NativeVibrationBinderTransportTest {
    private class Descriptors : VibrationBinderDescriptors {
        override val socketFd = 17
        override val pidfd = 19
        override val pid = 23
        override val uid = 29
        var closed = false
        override fun close() { closed = true }
    }

    private class Native : VibrationBinderNativeCalls {
        var openResult = 42L
        var nextResult: LongArray? = longArrayOf(2, 7, 3, 500)
        var completeResult = 0
        var stopResult = 0
        val scripted = java.util.ArrayDeque<LongArray>().apply { add(longArrayOf(3, 0, 0, 0)) }
        var header: ByteArray? = null
        var args: List<Int>? = null
        val destroyed = AtomicInteger()
        var completed: Triple<Long, Long, Boolean>? = null
        override fun open(socket: Int, pidfd: Int, pid: Int, uid: Int, header: ByteArray): Long {
            args = listOf(socket, pidfd, pid, uid); this.header = header.copyOf(); return openResult
        }
        override fun next(handle: Long): LongArray? = if (scripted.isEmpty()) nextResult else scripted.removeFirst()
        override fun complete(handle: Long, token: Long, success: Boolean): Int {
            completed = Triple(handle, token, success); return completeResult
        }
        override fun stop(handle: Long): Int = stopResult
        override fun destroy(handle: Long): Int { destroyed.incrementAndGet(); return 0 }
    }

    private fun header() = byteArrayOf(1, 2, 3, 4, 5, 6, 7, 8, 0x54, 0x53, 0x59, 0x53)

    @Test fun duplicatesAreReleasedAfterNativeOpenAndEventsAreMapped() {
        val descriptors = Descriptors(); val native = Native(); val bytes = header()
        val transport = NativeVibrationBinderTransport.open(descriptors, bytes, native)
        assertTrue(descriptors.closed)
        assertEquals(listOf(17, 19, 23, 29), native.args)
        assertArrayEquals(bytes, native.header)
        assertEquals(HostVibrationBinderEvent(7, 3, 500), transport.next())
        transport.complete(7, true)
        assertEquals(Triple(42L, 7L, true), native.completed)
        transport.close(); transport.close()
        assertEquals(1, native.destroyed.get())
    }

    @Test fun failureAndInvalidHeadersReleaseDescriptorsWithoutCreatingTransport() {
        for (bytes in listOf(ByteArray(11), ByteArray(12))) {
            val descriptors = Descriptors(); val native = Native()
            assertThrows(IllegalArgumentException::class.java) { NativeVibrationBinderTransport.open(descriptors, bytes, native) }
            assertTrue(descriptors.closed); assertNull(native.args); assertEquals(0, native.destroyed.get())
        }
        val descriptors = Descriptors(); val native = Native().apply { openResult = 0 }
        assertThrows(IOException::class.java) { NativeVibrationBinderTransport.open(descriptors, header(), native) }
        assertTrue(descriptors.closed); assertEquals(0, native.destroyed.get())
    }

    @Test fun leaseCloseFailureReleasesAlreadyOpenedNativeHandle() {
        val native = Native()
        val source = object : VibrationBinderDescriptors {
            override val socketFd = 17; override val pidfd = 19
            override val pid = 23; override val uid = 29
            override fun close() { throw IOException("lease close failed") }
        }
        assertThrows(IOException::class.java) { NativeVibrationBinderTransport.open(source, header(), native) }
        assertEquals(1, native.destroyed.get())
    }

    @Test fun invalidOrFailedNativeEventsDoNotBecomeSuccessfulRequests() {
        val invalid = listOf<LongArray?>(null, longArrayOf(), longArrayOf(-1, 0, 0, 0),
            longArrayOf(2, 0, 3, 500), longArrayOf(2, 7, 3, 5001), longArrayOf(2, 7, 4294967297L, 0),
            longArrayOf(2, 7, 1, 500), longArrayOf(3, 0, 0, 0))
        for (event in invalid) {
            val native = Native().apply { nextResult = event }
            val transport = NativeVibrationBinderTransport.open(Descriptors(), header(), native)
            assertThrows(IOException::class.java) { transport.next() }
            transport.close()
        }
        val native = Native().apply { nextResult = longArrayOf(1, 0, 0, 0); completeResult = 1 }
        val transport = NativeVibrationBinderTransport.open(Descriptors(), header(), native)
        assertNull(transport.next())
        assertThrows(IOException::class.java) { transport.complete(7, true) }
        transport.close()
    }

    @Test fun failedStopIsReportedAndCloseStillDestroysHandle() {
        val native = Native().apply { stopResult = -1 }
        val transport = NativeVibrationBinderTransport.open(Descriptors(), header(), native)
        assertThrows(IOException::class.java) { transport.requestStop() }
        transport.close()
        assertEquals(1, native.destroyed.get())
    }

    @Test fun registrationReceiptIsNotARequestAndCanBeAwaited() {
        val native = Native()
        val transport = NativeVibrationBinderTransport.open(Descriptors(), header(), native)
        assertFalse(transport.awaitRegistration(0))
        assertEquals(HostVibrationBinderEvent(7, 3, 500), transport.next())
        assertTrue(transport.awaitRegistration(0))
        transport.close()
        assertFalse(transport.awaitRegistration(0))
    }

    @Test fun duplicateRegistrationReceiptIsRejected() {
        val native = Native().apply {
            scripted.add(longArrayOf(3, 0, 0, 0))
        }
        val transport = NativeVibrationBinderTransport.open(Descriptors(), header(), native)
        assertThrows(IOException::class.java) { transport.next() }
        transport.close()
    }

    @Test fun eventBeforeRegistrationIsRejected() {
        val native = Native().apply { scripted.clear() }
        val transport = NativeVibrationBinderTransport.open(Descriptors(), header(), native)
        assertThrows(IOException::class.java) { transport.next() }
        assertFalse(transport.awaitRegistration(0))
        transport.close()
    }
}
