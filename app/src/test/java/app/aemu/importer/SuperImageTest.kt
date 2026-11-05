package app.aemu.importer

import org.junit.Assert.*
import org.junit.Test
import java.io.IOException
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.security.MessageDigest

class SuperImageTest {
    private class MemorySource(val bytes: ByteArray) : RandomSource {
        override val size = bytes.size.toLong()
        override fun read(pos: Long, dst: ByteBuffer) {
            for (i in 0 until dst.remaining()) dst.put(bytes[(pos + i).toInt()])
        }
    }

    private fun fixture(): ByteArray {
        val all = ByteArray(65536)
        val g = ByteBuffer.wrap(all, 4096, 4096).slice().order(ByteOrder.LITTLE_ENDIAN)
        g.putInt(0, 0x616c4467); g.putInt(4, 52); g.putInt(40, 4096); g.putInt(44, 2); g.putInt(48, 4096)
        val h = ByteBuffer.wrap(all, 12288, 128).slice().order(ByteOrder.LITTLE_ENDIAN)
        h.putInt(0, 0x414c5030); h.putShort(4, 10); h.putShort(6, 0); h.putInt(8, 128); h.putInt(44, 72)
        h.putInt(80, 0); h.putInt(84, 1); h.putInt(88, 48)
        h.putInt(92, 48); h.putInt(96, 1); h.putInt(100, 24)
        h.putInt(104, 72); h.putInt(108, 0); h.putInt(112, 48)
        h.putInt(116, 72); h.putInt(120, 0); h.putInt(124, 64)
        val t = ByteBuffer.wrap(all, 12416, 72).slice().order(ByteOrder.LITTLE_ENDIAN)
        t.put("system_a".toByteArray()); t.putInt(40, 0); t.putInt(44, 1)
        t.putLong(48, 1); t.putInt(56, 0); t.putLong(60, 64); t.putInt(68, 0)
        seal(all)
        return all
    }
    private fun seal(all: ByteArray) {
        fun digest(start: Int, size: Int) = MessageDigest.getInstance("SHA-256").digest(all.copyOfRange(start, start + size))
        java.util.Arrays.fill(all, 4104, 4136, 0.toByte())
        System.arraycopy(digest(4096, 52), 0, all, 4104, 32)
        System.arraycopy(digest(12416, 72), 0, all, 12336, 32)
        java.util.Arrays.fill(all, 12300, 12332, 0.toByte())
        System.arraycopy(digest(12288, 128), 0, all, 12300, 32)
    }
    @Test fun readsValidLinearExtent() {
        assertEquals(listOf(32768L to 512L), SuperImage.partitions(MemorySource(fixture()))["system_a"])
    }
    @Test fun rejectsDamagedTables() {
        val b = fixture(); b[12416] = (b[12416].toInt() xor 1).toByte()
        assertThrows(IOException::class.java) { SuperImage.partitions(MemorySource(b)) }
    }
    @Test fun rejectsExtentPastEndOfSource() {
        val b = fixture(); ByteBuffer.wrap(b).order(ByteOrder.LITTLE_ENDIAN).putLong(12416 + 60, 999999)
        seal(b)
        assertThrows(IOException::class.java) { SuperImage.partitions(MemorySource(b)) }
    }
    @Test fun rejectsMissingOrUnsupportedExtents() {
        for (badType in listOf(1, 2)) {
            val b = fixture(); ByteBuffer.wrap(b).order(ByteOrder.LITTLE_ENDIAN).putInt(12416 + 56, badType); seal(b)
            assertThrows(IOException::class.java) { SuperImage.partitions(MemorySource(b)) }
        }
        val b = fixture(); ByteBuffer.wrap(b).order(ByteOrder.LITTLE_ENDIAN).putInt(12416 + 40, 4); seal(b)
        assertThrows(IOException::class.java) { SuperImage.partitions(MemorySource(b)) }
    }
    @Test fun rejectsOversizedTableBeforeAllocation() {
        val b = fixture(); ByteBuffer.wrap(b).order(ByteOrder.LITTLE_ENDIAN).putInt(12288 + 44, Int.MAX_VALUE)
        assertThrows(IOException::class.java) { SuperImage.partitions(MemorySource(b)) }
    }
}
