package app.aemu.core

import java.io.ByteArrayOutputStream
import java.nio.ByteBuffer
import java.nio.charset.StandardCharsets
import java.time.ZonedDateTime
import kotlin.math.abs

/** Encodes a single, local synthetic incoming SMS; it never contacts a network. */
internal object VirtualSmsDeliver {
    fun isTextLengthSupported(text: String): Boolean = text.length <= 70

    fun encode(sender: String, text: String, time: ZonedDateTime): ByteArray {
        val international = sender.startsWith('+')
        val digits = if (international) sender.substring(1) else sender
        require(digits.length in 1..20 && digits.all { it in '0'..'9' })
        require(isTextLengthSupported(text)) { "Multipart SMS is not supported" }
        var index = 0
        while (index < text.length) {
            val character = text[index]
            when {
                Character.isHighSurrogate(character) -> {
                    require(index + 1 < text.length && Character.isLowSurrogate(text[index + 1]))
                    index += 2
                }
                Character.isLowSurrogate(character) -> throw IllegalArgumentException("Unpaired surrogate")
                else -> index++
            }
        }
        val seconds = time.offset.totalSeconds
        require(time.year in 2000..2099 && seconds % 900 == 0 && abs(seconds) <= 14 * 3600)
        val out = ByteArrayOutputStream()
        out.write(0) // No SMSC address.
        out.write(4) // SMS-DELIVER.
        out.write(digits.length)
        out.write(if (international) 0x91 else 0x81)
        for (i in digits.indices step 2) {
            val low = digits[i] - '0'
            val high = if (i + 1 < digits.length) digits[i + 1] - '0' else 0xf
            out.write(low or (high shl 4))
        }
        out.write(0) // PID.
        out.write(8) // UCS-2 / UTF-16BE user data.
        for (value in intArrayOf(time.year % 100, time.monthValue, time.dayOfMonth,
            time.hour, time.minute, time.second)) out.write(swappedDecimal(value))
        out.write(swappedDecimal(abs(seconds) / 900) or if (seconds < 0) 0x08 else 0)
        val body = text.toByteArray(StandardCharsets.UTF_16BE)
        out.write(body.size)
        out.write(body)
        return out.toByteArray()
    }

    /** Frame accepted by the isolated radio event protocol. */
    fun frame(epoch: Long, sequence: Long, pdu: ByteArray): ByteArray {
        require(epoch != 0L && sequence > 0 && pdu.size in 1..256)
        return ByteBuffer.allocate(23 + pdu.size)
            .put(byteArrayOf(82, 77, 69, 49))
            .putLong(epoch)
            .putLong(sequence)
            .put(1.toByte())
            .putShort(pdu.size.toShort())
            .put(pdu)
            .array()
    }

    private fun swappedDecimal(value: Int): Int = ((value % 10) shl 4) or (value / 10)
}
