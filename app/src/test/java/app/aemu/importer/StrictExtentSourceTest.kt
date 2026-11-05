package app.aemu.importer

import org.junit.Assert.*
import org.junit.Test
import java.io.IOException
import java.nio.ByteBuffer

class StrictExtentSourceTest {
    private class Source(private val b: ByteArray) : RandomSource {
        override val size = b.size.toLong()
        override fun read(pos: Long, dst: ByteBuffer) {
            if (pos < 0 || pos > size || dst.remaining().toLong() > size - pos) throw IOException("outside source")
            dst.put(b, pos.toInt(), dst.remaining())
        }
    }
    @Test fun readsAcrossExtents() {
        val s = ExtentSource(Source("abcdefgh".toByteArray()), listOf(1L to 2L, 5L to 2L), strict = true)
        val dst = ByteBuffer.allocate(4); s.read(0, dst); assertEquals("bcfg", String(dst.array()))
    }
    @Test fun rejectsNegativeOrOutOfBoundsReads() {
        val s = ExtentSource(Source(ByteArray(8)), listOf(0L to 4L), strict = true)
        assertThrows(IOException::class.java) { s.read(-1, ByteBuffer.allocate(1)) }
        assertThrows(IOException::class.java) { s.read(4, ByteBuffer.allocate(1)) }
    }
    @Test fun rejectsInvalidExtentsAndOverflow() {
        assertThrows(IOException::class.java) { ExtentSource(Source(ByteArray(8)), listOf(7L to 2L), strict = true) }
        assertThrows(IOException::class.java) { ExtentSource(Source(ByteArray(8)), listOf(-1L to 2L), strict = true) }
    }
}
