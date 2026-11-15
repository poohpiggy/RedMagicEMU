package app.aemu.core

import java.io.ByteArrayOutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.Base64

/** Protocol used by Android's resNetworkSend/resNetworkResult over dnsproxyd. */
internal object DnsRawProtocol {
    // The existing dnsproxyd command reader accepts at most 1024 bytes including the NUL.
    private const val MAX_COMMAND_BYTES = 1024
    private const val MAX_ANSWER = 65535
    private const val EINVAL = 22
    private const val EIO = 5

    sealed class Result {
        data class Answer(val rcode: Int, val packet: ByteArray) : Result()
        data class Error(val errno: Int) : Result()
    }

    fun error(errno: Int): ByteArray = intBytes(-(if (errno in 1..4095) errno else EIO))

    fun reply(args: List<String>, resolve: (Long, ByteArray, Int) -> Result): ByteArray {
        if (args.size != 4 || args[0] != "resnsend") return error(EINVAL)
        // Guest IDs (including bit 31 for private DNS bypass) are not host network IDs.
        val guestNetId = args[1].toLongOrNull() ?: return error(EINVAL)
        if (guestNetId !in 0..0xffffffffL) return error(EINVAL)
        val flags = args[2].toIntOrNull() ?: return error(EINVAL)
        if (flags !in 0..7 || args.sumOf { it.toByteArray(Charsets.UTF_8).size } + 3 + 1 > MAX_COMMAND_BYTES)
            return error(EINVAL)
        val packet = try { Base64.getDecoder().decode(args[3]) } catch (_: IllegalArgumentException) {
            return error(EINVAL)
        }
        if (packet.size < 12) return error(EINVAL)
        val result = resolve(guestNetId, packet, flags)
        return when (result) {
            is Result.Error -> error(result.errno)
            is Result.Answer -> {
                if (result.rcode !in 0..65535 || result.packet.size !in 12..MAX_ANSWER) return error(EIO)
                ByteArrayOutputStream(8 + result.packet.size).apply {
                    write(intBytes(result.rcode))
                    write(intBytes(result.packet.size))
                    write(result.packet)
                }.toByteArray()
            }
        }
    }

    private fun intBytes(value: Int): ByteArray = ByteBuffer.allocate(4)
        .order(ByteOrder.BIG_ENDIAN).putInt(value).array()
}
