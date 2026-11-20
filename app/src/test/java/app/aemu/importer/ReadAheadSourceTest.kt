package app.aemu.importer

import java.io.IOException
import java.nio.ByteBuffer
import org.junit.Assert.*
import org.junit.Test

class ReadAheadSourceTest {
    @Test fun reusesNearbyReadsAcrossBoundariesAndHandlesTheFinalPartialWindow() {
        val window = 256 * 1024
        val bytes = ByteArray(2 * window + 7) { (it % 251).toByte() }
        var reads = 0
        val source = ReadAheadSource(object : RandomSource {
            override val size = bytes.size.toLong()
            override fun read(pos: Long, dst: ByteBuffer) {
                assertTrue("A cache miss should read directly into the reusable I/O buffer", dst.isDirect)
                reads++
                dst.put(bytes, pos.toInt(), dst.remaining())
            }
        })
        for ((offset, count) in listOf(8 to 16, window - 3 to 6, 200 to 50, 2 * window + 4 to 3)) {
            val actual = ByteArray(count)
            source.read(offset.toLong(), ByteBuffer.wrap(actual))
            assertArrayEquals(bytes.copyOfRange(offset, offset + count), actual)
        }
        assertEquals(3, reads)
        assertThrows(IOException::class.java) { source.read(bytes.size.toLong(), ByteBuffer.allocate(1)) }
    }

    @Test fun truncatedFetchIsNeverCached() {
        var reads = 0
        val source = ReadAheadSource(object : RandomSource {
            override val size = 64L
            override fun read(pos: Long, dst: ByteBuffer) {
                reads++
                if (reads == 1) dst.put(42)
                else while (dst.hasRemaining()) dst.put(17)
            }
        })
        assertThrows(IOException::class.java) { source.read(1, ByteBuffer.allocate(1)) }
        val actual = ByteBuffer.allocate(1)
        source.read(1, actual)
        assertEquals(17, actual.get(0).toInt())
        assertEquals(2, reads)
    }
}
