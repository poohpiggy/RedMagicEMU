package app.aemu.core

import org.junit.Assert.*
import org.junit.Test
import java.io.ByteArrayOutputStream
import java.io.IOException
import java.io.OutputStream

class RilDeliveryTest {
    private fun stub(out: OutputStream?): RilStub {
        val cls = Class.forName("sun.misc.Unsafe")
        val field = cls.getDeclaredField("theUnsafe").apply { isAccessible = true }
        val instance = cls.getMethod("allocateInstance", Class::class.java)
            .invoke(field.get(null), RilStub::class.java) as RilStub
        RilStub::class.java.getDeclaredField("out").apply { isAccessible = true }.set(instance, out)
        return instance
    }

    @Test fun noConnectionReportsFailure() {
        assertFalse(stub(null).sms("+15550001234", "test"))
        assertFalse(stub(null).ring("+15550001234"))
    }

    @Test fun brokenConnectionMustNotReportDelivery() {
        val broken = object : OutputStream() {
            override fun write(b: Int) { throw IOException("disconnected") }
        }
        assertFalse(stub(broken).sms("+15550001234", "test"))
        assertFalse(stub(broken).ring("+15550001234"))
    }

    @Test fun secondCallEventFailureMustNotReportDelivery() {
        var callStateSeenAfterFirstEvent: Pair<*, *>? = null
        lateinit var ril: RilStub
        val output = object : OutputStream() {
            var writes = 0
            override fun write(b: Int) { throw AssertionError("unexpected single-byte write") }
            override fun write(b: ByteArray, off: Int, len: Int) {
                if (++writes > 2) throw IOException("disconnected")
            }
            override fun flush() {
                val call = RilStub::class.java.getDeclaredField("call").apply { isAccessible = true }
                callStateSeenAfterFirstEvent = call.get(ril) as Pair<*, *>?
            }
        }
        ril = stub(output)
        assertFalse(ril.ring("+15550001234"))
        assertEquals("+15550001234", callStateSeenAfterFirstEvent?.first)
        val afterFailure = RilStub::class.java.getDeclaredField("call").apply { isAccessible = true }.get(ril) as Pair<*, *>?
        assertEquals("a first event may already have reached the guest", callStateSeenAfterFirstEvent, afterFailure)
    }

    @Test fun multipartSmsFailureMustNotReportDelivery() {
        val output = object : OutputStream() {
            var writes = 0
            override fun write(b: Int) { throw AssertionError("unexpected single-byte write") }
            override fun write(b: ByteArray, off: Int, len: Int) {
                if (++writes > 2) throw IOException("disconnected")
            }
        }
        assertFalse(stub(output).sms("+15550001234", "a".repeat(71)))
    }

    @Test fun failedRingDoesNotUndoPeerAnswer() {
        lateinit var ril: RilStub
        val output = object : OutputStream() {
            var writes = 0
            override fun write(b: Int) { throw AssertionError("unexpected single-byte write") }
            override fun write(b: ByteArray, off: Int, len: Int) {
                if (++writes > 2) throw IOException("disconnected")
            }
            override fun flush() {
                RilStub::class.java.getDeclaredField("call").apply { isAccessible = true }
                    .set(ril, "+15550001234" to 0)
            }
        }
        ril = stub(output)
        assertFalse(ril.ring("+15550001234"))
        val afterFailure = RilStub::class.java.getDeclaredField("call").apply { isAccessible = true }.get(ril)
        assertEquals("+15550001234" to 0, afterFailure)
    }

    @Test fun workingConnectionSendsSmsAndRing() {
        val smsOut = ByteArrayOutputStream()
        assertTrue(stub(smsOut).sms("+15550001234", "test"))
        assertTrue(smsOut.size() > 8)
        val callOut = ByteArrayOutputStream()
        assertTrue(stub(callOut).ring("+15550001234"))
        assertTrue(callOut.size() >= 24)
    }
}
