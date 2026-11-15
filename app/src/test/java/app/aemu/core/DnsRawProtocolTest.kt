package app.aemu.core

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Test
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.Base64

class DnsRawProtocolTest {
    private val query = byteArrayOf(0x12, 0x34, 0x01, 0x00, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1)
    private val answer = query.copyOf().also { it[2] = 0x81.toByte() }
    private fun command(netId: String = "101", flags: String = "0", payload: ByteArray = query) =
        listOf("resnsend", netId, flags, Base64.getEncoder().encodeToString(payload))
    private fun intBytes(value: Int) = ByteBuffer.allocate(4).order(ByteOrder.BIG_ENDIAN).putInt(value).array()

    @Test fun guestNetworkIdentifierIsPassedToTheAuthorizedResolver() {
        var seen = -1L
        DnsRawProtocol.reply(command("2147483749", "0")) { id, _, _ ->
            seen = id
            DnsRawProtocol.Result.Answer(0, answer)
        }
        assertEquals(2147483749L, seen)
    }

    @Test fun validResponseUsesBinaryRcodeLengthAndAnswer() {
        var calls = 0
        val reply = DnsRawProtocol.reply(command("2147483749", "5")) { _, packet, flags ->
            calls++
            assertEquals(5, flags)
            assertArrayEquals(query, packet)
            DnsRawProtocol.Result.Answer(0, answer)
        }
        assertEquals(1, calls)
        assertArrayEquals(intBytes(0) + intBytes(answer.size) + answer, reply)
    }

    @Test fun dnsResponseCodesRemainRealAndAreNotAscii() {
        val reply = DnsRawProtocol.reply(command()) { _, _, _ -> DnsRawProtocol.Result.Answer(3, answer) }
        assertArrayEquals(intBytes(3) + intBytes(answer.size) + answer, reply)
    }

    @Test fun malformedInputsFailWithoutCallingResolver() {
        val cases = listOf(
            listOf("resnsend"), command(netId = "-1"), command(netId = "4294967296"),
            command(flags = "8"), listOf("resnsend", "101", "0", "!"),
            command(payload = byteArrayOf(1, 2)), command() + "extra"
        )
        for (args in cases) {
            val reply = DnsRawProtocol.reply(args) { _, _, _ -> throw AssertionError("resolver called") }
            assertArrayEquals(intBytes(-22), reply)
        }
    }

    @Test fun packetFitsWhenTheWholeCommandIncludingTerminatorFits() {
        val largerQuery = query.copyOf(753) // base64 is 1004 bytes, command is 1018 with NUL
        val reply = DnsRawProtocol.reply(command("0", "0", largerQuery)) { _, packet, _ ->
            assertArrayEquals(largerQuery, packet)
            DnsRawProtocol.Result.Answer(0, answer)
        }
        assertArrayEquals(intBytes(0) + intBytes(answer.size) + answer, reply)
        val tooLarge = command("0", "0", query.copyOf(768))
        assertArrayEquals(intBytes(-22), DnsRawProtocol.reply(tooLarge) { _, _, _ ->
            throw AssertionError("oversized query forwarded")
        })
    }

    @Test fun errorsAreNegativeErrnoAndDoNotPretendDnsSuccess() {
        assertArrayEquals(intBytes(-110), DnsRawProtocol.reply(command()) { _, _, _ -> DnsRawProtocol.Result.Error(110) })
        assertArrayEquals(intBytes(-5), DnsRawProtocol.reply(command()) { _, _, _ -> DnsRawProtocol.Result.Error(0) })
        assertArrayEquals(intBytes(-5), DnsRawProtocol.reply(command()) { _, _, _ -> DnsRawProtocol.Result.Answer(0, byteArrayOf()) })
    }

    @Test(expected = IllegalStateException::class)
    fun unexpectedResolverFailureIsPropagatedForTheSocketHandlerToLog() {
        DnsRawProtocol.reply(command()) { _, _, _ -> throw IllegalStateException("failure") }
    }
}
