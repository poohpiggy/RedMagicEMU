package app.aemu.importer

import java.io.IOException
import java.nio.ByteBuffer
import java.nio.channels.SeekableByteChannel

/** Input for verified firmware: a truncated or out-of-range read is never treated as zeroes. */
internal class StrictChannelSource(private val ch: SeekableByteChannel) : RandomSource {
    override val size: Long = ch.size()

    override fun read(pos: Long, dst: ByteBuffer) {
        if (pos < 0 || pos > size || dst.remaining().toLong() > size - pos)
            throw IOException("firmware image read outside bounds")
        synchronized(ch) {
            ch.position(pos)
            while (dst.hasRemaining()) {
                if (ch.read(dst) <= 0) throw IOException("firmware image was truncated or could not be read")
            }
        }
    }
}
