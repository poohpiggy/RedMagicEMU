package app.aemu.importer

import java.io.IOException
import java.nio.ByteBuffer

/** Bounded read-ahead for an immutable image being traversed by a single thread. */
internal class ReadAheadSource(private val source: RandomSource) : RandomSource {
    override val size: Long = source.size
    private val windows = Array(4) { Window() }
    private var nextWindow = 0

    override fun read(pos: Long, dst: ByteBuffer) {
        if (pos < 0 || pos > size || dst.remaining().toLong() > size - pos)
            throw IOException("Firmware image read outside bounds")
        if (dst.remaining() >= WINDOW_BYTES) {
            source.read(pos, dst)
            if (dst.hasRemaining()) throw IOException("Firmware image was truncated")
            return
        }

        var offset = pos
        while (dst.hasRemaining()) {
            val start = offset / WINDOW_BYTES * WINDOW_BYTES
            val window = windows.firstOrNull { it.start == start } ?: load(start)
            val index = (offset - start).toInt()
            val count = minOf(dst.remaining(), window.length - index)
            window.buffer.limit(index + count)
            window.buffer.position(index)
            dst.put(window.buffer)
            offset += count
        }
    }

    private fun load(start: Long): Window {
        val window = windows[nextWindow]
        nextWindow = (nextWindow + 1) % windows.size
        window.start = -1
        val length = minOf(WINDOW_BYTES.toLong(), size - start).toInt()
        val input = window.buffer
        input.clear()
        input.limit(length)
        source.read(start, input)
        if (input.hasRemaining()) throw IOException("Firmware image was truncated")
        window.length = length
        window.start = start
        return window
    }

    private class Window {
        val buffer: ByteBuffer = ByteBuffer.allocateDirect(WINDOW_BYTES)
        var length = 0
        var start = -1L
    }

    companion object {
        private const val WINDOW_BYTES = 256 * 1024
    }
}
