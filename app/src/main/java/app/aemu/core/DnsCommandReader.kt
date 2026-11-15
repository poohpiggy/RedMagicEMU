package app.aemu.core

import java.io.ByteArrayOutputStream
import java.io.InputStream
import java.net.SocketTimeoutException
import java.util.concurrent.TimeUnit

/** Reads one DNS command with a total deadline, including slow per-byte senders. */
internal object DnsCommandReader {
    data class Request(val bytes: ByteArray, val terminated: Boolean)
    fun read(input: InputStream, timeoutMs: Int, setTimeout: (Int) -> Unit,
             nanoTime: () -> Long = System::nanoTime): Request {
        require(timeoutMs > 0)
        val start = nanoTime()
        val budget = TimeUnit.MILLISECONDS.toNanos(timeoutMs.toLong())
        val request = ByteArrayOutputStream()
        while (request.size() < 1024) {
            val remaining = budget - (nanoTime() - start)
            if (remaining <= 0) throw SocketTimeoutException("DNS command deadline")
            val millis = ((remaining + 999_999L) / 1_000_000L).coerceIn(1L, timeoutMs.toLong()).toInt()
            setTimeout(millis)
            val b = input.read()
            if (b <= 0) return Request(request.toByteArray(), b == 0)
            request.write(b)
        }
        return Request(request.toByteArray(), false)
    }
}
