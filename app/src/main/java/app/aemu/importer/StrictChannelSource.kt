package app.aemu.importer

import java.io.IOException
import java.nio.ByteBuffer
import java.nio.channels.FileChannel

/** Input for verified firmware: a truncated or out-of-range read is never treated as zeroes. */
internal class StrictChannelSource(
    private val ch: FileChannel,
    override val size: Long = ch.size(),
) : RandomSource {
    override fun read(pos: Long, dst: ByteBuffer) {
        if (pos < 0 || pos > size || dst.remaining().toLong() > size - pos)
            throw IOException("firmware image read outside bounds")
        var offset = pos
        while (dst.hasRemaining()) {
            val count = ch.read(dst, offset)
            if (count <= 0) throw IOException("firmware image was truncated or could not be read")
            offset += count
        }
    }
}
