package app.aemu.core

import org.junit.Assert.*
import org.junit.Test
import java.time.ZoneOffset
import java.time.ZonedDateTime

class VirtualSmsDeliverTest {
    private val at = ZonedDateTime.of(2026, 11, 7, 12, 0, 0, 0, ZoneOffset.UTC)

    @Test fun encodesSyntheticGsmDeliverFixture() {
        val expected = byteArrayOf(0, 4, 5, 0x91.toByte(), 1, 0x80.toByte(), 0xf6.toByte(), 0, 8,
            0x62, 0x11, 0x70, 0x21, 0, 0, 0, 2, 0, 0x41)
        assertArrayEquals(expected, VirtualSmsDeliver.encode("+10086", "A", at))
        val unicode = byteArrayOf(0, 4, 5, 0x91.toByte(), 1, 0x80.toByte(), 0xf6.toByte(), 0, 8,
            0x62, 0x11, 0x70, 0x21, 0, 0, 0, 6, 0x4f, 0x60, 0xd8.toByte(), 0x3d,
            0xde.toByte(), 0)
        assertArrayEquals(unicode, VirtualSmsDeliver.encode("+10086", "你😀", at))
    }

    @Test fun framesTheExactRawPduForTheRadioEventProtocol() {
        val pdu = VirtualSmsDeliver.encode("+10086", "A", at)
        val frame = VirtualSmsDeliver.frame(77, 1, pdu)
        assertArrayEquals(byteArrayOf(82, 77, 69, 49), frame.copyOfRange(0, 4))
        assertEquals(77L, java.nio.ByteBuffer.wrap(frame, 4, 8).long)
        assertEquals(1L, java.nio.ByteBuffer.wrap(frame, 12, 8).long)
        assertEquals(1, frame[20].toInt())
        assertEquals(pdu.size, ((frame[21].toInt() and 255) shl 8) or (frame[22].toInt() and 255))
        assertArrayEquals(pdu, frame.copyOfRange(23, frame.size))
        val signedEpoch = VirtualSmsDeliver.frame(Long.MIN_VALUE, 1, pdu)
        assertEquals(Long.MIN_VALUE, java.nio.ByteBuffer.wrap(signedEpoch, 4, 8).long)
    }

    @Test fun allowsCompleteSurrogatePairsButRejectsMalformedOrMultipart() {
        assertTrue(VirtualSmsDeliver.encode("10086", "A".repeat(68) + "😀", at).size > 0)
        rejects { VirtualSmsDeliver.encode("10086", "A".repeat(69) + "😀", at) }
        rejects { VirtualSmsDeliver.encode("10086", "A".repeat(71), at) }
        rejects { VirtualSmsDeliver.encode("10086", "\uD83D", at) }
        rejects { VirtualSmsDeliver.encode("10086", "\uDE00", at) }
    }

    @Test fun inputLengthCheckMatchesSingleMessageEncoding() {
        for (body in listOf("", "你".repeat(70), "A".repeat(68) + "😀")) {
            assertTrue(VirtualSmsDeliver.isTextLengthSupported(body))
            assertTrue(VirtualSmsDeliver.encode("10086", body, at).isNotEmpty())
        }
        for (body in listOf("你".repeat(71), "A".repeat(69) + "😀")) {
            assertFalse(VirtualSmsDeliver.isTextLengthSupported(body))
            rejects { VirtualSmsDeliver.encode("10086", body, at) }
        }
    }

    @Test fun rejectsMalformedAddressTimeAndFrameParameters() {
        for (number in listOf("", "+", "12-3", "１２３", "1".repeat(21)))
            rejects { VirtualSmsDeliver.encode(number, "A", at) }
        rejects { VirtualSmsDeliver.encode("123", "A", at.withYear(2100)) }
        rejects { VirtualSmsDeliver.encode("123", "A", at.withZoneSameInstant(ZoneOffset.ofHoursMinutes(5, 20))) }
        rejects { VirtualSmsDeliver.frame(0, 1, byteArrayOf(1)) }
        rejects { VirtualSmsDeliver.frame(1, 0, byteArrayOf(1)) }
        rejects { VirtualSmsDeliver.frame(1, 1, ByteArray(257)) }
    }

    @Test fun encodesNegativeQuarterHourTimezone() {
        val timestamp = at.withZoneSameInstant(ZoneOffset.ofHoursMinutes(-5, -30))
        val pdu = VirtualSmsDeliver.encode("123", "A", timestamp)
        assertEquals(0x2a, pdu[pdu.size - 4].toInt() and 255)
    }

    private fun rejects(block: () -> Unit) {
        try { block(); fail("invalid input accepted") } catch (_: IllegalArgumentException) { }
    }
}
