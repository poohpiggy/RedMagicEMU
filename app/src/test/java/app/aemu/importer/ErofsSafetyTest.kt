package app.aemu.importer

import org.junit.Assert.*
import org.junit.Test
import java.io.IOException
import java.nio.ByteBuffer
import java.nio.ByteOrder

class ErofsSafetyTest {
    private open class MemorySource(val bytes: ByteArray) : RandomSource {
        override val size = bytes.size.toLong()
        override fun read(pos: Long, dst: ByteBuffer) {
            if (pos < 0 || pos + dst.remaining() > size) throw IOException("out of range")
            dst.put(bytes, pos.toInt(), dst.remaining())
        }
    }
    private fun image(dirSize: Int): ByteArray {
        val b = ByteArray(16384); val x = ByteBuffer.wrap(b).order(ByteOrder.LITTLE_ENDIAN)
        x.putInt(1024, 0xe0f5e1e2.toInt()); x.put(1036, 12); x.putShort(1038, 0); x.putInt(1064, 1)
        x.putShort(4096, 0); x.putShort(4100, 0x41ed); x.putInt(4104, dirSize); x.putInt(4112, 2)
        return b
    }
    @Test fun readsValidDirectoryAndFile() {
        val b = image(29); val x = ByteBuffer.wrap(b).order(ByteOrder.LITTLE_ENDIAN)
        x.putLong(8192, 0); x.putShort(8200, 24)
        x.putLong(8204, 2); x.putShort(8212, 25)
        b[8216] = '.'.code.toByte(); System.arraycopy("file".toByteArray(), 0, b, 8217, 4)
        x.putShort(4164, 0x81a4.toShort()); x.putInt(4168, 3); x.putInt(4176, 3)
        System.arraycopy("abc".toByteArray(), 0, b, 12288, 3)
        val found = mutableMapOf<String, String>()
        ErofsImage(MemorySource(b)).walk { name, _, _, _, _, data ->
            if (data != null) found[name] = data().use { String(it.readBytes()) }
        }
        assertEquals(mapOf("file" to "abc"), found)
    }
    @Test fun rejectsHugeDirectoryWithoutAllocation() {
        assertThrows(IOException::class.java) { ErofsImage(MemorySource(image(Int.MAX_VALUE))).walk { _, _, _, _, _, _ -> } }
    }
    @Test fun rejectsShortDirectoryEntry() {
        assertThrows(IOException::class.java) { ErofsImage(MemorySource(image(3))).walk { _, _, _, _, _, _ -> } }
    }
    @Test fun rejectsInvalidName() {
        val b = image(16); val x = ByteBuffer.wrap(b).order(ByteOrder.LITTLE_ENDIAN)
        x.putLong(8192, 1); x.putShort(8200, 12); b[8204] = 'a'.code.toByte(); b[8205] = '/'.code.toByte(); b[8206] = 'b'.code.toByte()
        assertThrows(IOException::class.java) { ErofsImage(MemorySource(b)).walk { _, _, _, _, _, _ -> } }
    }
    private fun fileImage(layout: Int, size: Long): ByteArray {
        val b = image(29); val x = ByteBuffer.wrap(b).order(ByteOrder.LITTLE_ENDIAN)
        x.putLong(8192, 0); x.putShort(8200, 24); x.putLong(8204, 2); x.putShort(8212, 25)
        b[8216] = '.'.code.toByte(); System.arraycopy("file".toByteArray(), 0, b, 8217, 4)
        x.putShort(4160, (1 or (layout shl 1)).toShort()); x.putShort(4164, 0x81a4.toShort()); x.putLong(4168, size)
        return b
    }
    @Test fun combinesAdjacentExtentsIntoOneRead() {
        val block = 4096
        val image = fileImage(1, 2L * block).copyOf(5 * block)
        val index = ByteBuffer.wrap(image).order(ByteOrder.LITTLE_ENDIAN)
        index.putInt(4244, 3)
        index.putInt(4252, 4)
        val expected = ByteArray(2 * block) { (it / block + 1).toByte() }
        expected.copyInto(image, 3 * block)
        val destinations = mutableListOf<ByteArray>()
        val source = object : MemorySource(image) {
            override fun read(pos: Long, dst: ByteBuffer) {
                if (pos >= 3L * block) destinations.add(dst.array())
                super.read(pos, dst)
            }
        }
        ErofsImage(source).walk { _, _, _, _, _, data ->
            data?.invoke()?.use { input ->
                val actual = ByteArray(expected.size + 4)
                assertEquals(expected.size, input.read(actual, 2, expected.size))
                assertArrayEquals(expected, actual.copyOfRange(2, expected.size + 2))
                assertEquals(1, destinations.size)
                assertSame(actual, destinations.single())
                assertEquals(0, input.read(actual, 2, 0))
                assertEquals(-1, input.read())
            }
        }
    }

    @Test fun keepsPlainExtentsSeparateWhenThePhysicalBlocksHaveAGap() {
        val block = 4096
        val image = fileImage(1, 2L * block).copyOf(6 * block)
        val index = ByteBuffer.wrap(image).order(ByteOrder.LITTLE_ENDIAN)
        index.putInt(4244, 3)
        index.putInt(4252, 5)
        image.fill(1, 3 * block, 4 * block)
        image.fill(2, 5 * block, 6 * block)
        val offsets = mutableListOf<Long>()
        val source = object : MemorySource(image) {
            override fun read(pos: Long, dst: ByteBuffer) {
                if (pos >= 3L * block) offsets.add(pos)
                super.read(pos, dst)
            }
        }
        ErofsImage(source).walk { _, _, _, _, _, data ->
            data?.invoke()?.use { input ->
                val actual = ByteArray(2 * block)
                assertEquals(actual.size, input.read(actual))
                assertArrayEquals(ByteArray(2 * block) { if (it < block) 1 else 2 }, actual)
            }
        }
        assertEquals(listOf(3L * block, 5L * block), offsets)
    }

    @Test fun lz4ExtentsRespectOutputOffsetsAndKeepPartialReads() {
        val block = 4096
        val image = fileImage(1, 2L * block).copyOf(5 * block)
        val index = ByteBuffer.wrap(image).order(ByteOrder.LITTLE_ENDIAN)
        for (i in 0..1) {
            index.putShort(4240 + i * 8, 1)
            index.putInt(4244 + i * 8, 3 + i)
            // Three literal bytes followed by a match at distance three that fills the extent.
            val literal = if (i == 0) "ABC" else "XYZ"
            val compressed = byteArrayOf(0x3f, literal[0].code.toByte(), literal[1].code.toByte(),
                literal[2].code.toByte(), 3, 0) + ByteArray(15) { 0xff.toByte() } + byteArrayOf(249.toByte())
            compressed.copyInto(image, (3 + i) * block)
        }
        val expected = ByteArray(2 * block) { i -> (if (i < block) "ABC" else "XYZ")[i % block % 3].code.toByte() }
        ErofsImage(MemorySource(image)).walk { _, _, _, _, _, data ->
            data?.invoke()?.use { input ->
                val actual = ByteArray(expected.size + 8) { 0x55 }
                assertEquals(block + 3, input.read(actual, 4, block + 3))
                assertEquals(block - 3, input.read(actual, 4 + block + 3, block - 3))
                assertEquals(-1, input.read())
                assertArrayEquals(expected, actual.copyOfRange(4, expected.size + 4))
                assertEquals(0x55.toByte(), actual[3])
                assertEquals(0x55.toByte(), actual[expected.size + 4])
            }
        }

        // A match at the very beginning of the second extent must not use the previous extent.
        val invalidMatch = byteArrayOf(0x0f, 1, 0) + ByteArray(15) { 0xff.toByte() } + byteArrayOf(252.toByte())
        invalidMatch.copyInto(image, 4 * block)
        val error = assertThrows(IOException::class.java) {
            ErofsImage(MemorySource(image)).walk { _, _, _, _, _, data ->
                data?.invoke()?.use { it.read(ByteArray(2 * block), 0, 2 * block) }
            }
        }
        assertEquals("EROFS: broken LZ4 data", error.message)
    }
    @Test fun cancellationIsCheckedWithinALargeLz4Extent() {
        val block = 4096
        val size = 128 * 1024
        val image = fileImage(1, size.toLong())
        val index = ByteBuffer.wrap(image).order(ByteOrder.LITTLE_ENDIAN)
        index.putShort(4240, 1)
        index.putInt(4244, 3)
        for (i in 1 until size / block) index.putShort(4240 + i * 8, 2)
        val token = byteArrayOf(0x1f, 'x'.code.toByte(), 1, 0, 236.toByte())
        repeat(size / 256) { token.copyInto(image, 3 * block + it * token.size) }

        var decoding = false
        var checks = 0
        val source = object : MemorySource(image) {
            override fun read(pos: Long, dst: ByteBuffer) {
                super.read(pos, dst)
                if (pos == 3L * block) decoding = true
            }
        }
        ErofsImage(source) { decoding && ++checks == 2 }.walk { _, _, _, _, _, data ->
            data?.invoke()?.use { input ->
                val actual = ByteArray(size)
                assertThrows(ImportCancelledException::class.java) { input.read(actual) }
                assertTrue(actual.take(64 * 1024).all { it == 'x'.code.toByte() })
                assertEquals(0.toByte(), actual[64 * 1024])
            }
        }
    }
    @Test fun rejectsOversizedCompressedIndexBeforeAllocation() {
        val b = fileImage(1, Long.MAX_VALUE)
        assertThrows(IOException::class.java) { ErofsImage(MemorySource(b)).walk { _, _, _, _, _, data -> data?.invoke() } }
    }
    @Test fun rejectsOversizedChunkTableBeforeAllocation() {
        val b = fileImage(4, Long.MAX_VALUE)
        assertThrows(IOException::class.java) { ErofsImage(MemorySource(b)).walk { _, _, _, _, _, data -> data?.invoke() } }
    }
    @Test fun rejectsFlatDataOutsideImage() {
        val b = fileImage(0, 50_000)
        assertThrows(IOException::class.java) { ErofsImage(MemorySource(b)).walk { _, _, _, _, _, data -> data?.invoke()?.use { it.readBytes() } } }
    }
    @Test fun cancelledTraversalStops() {
        assertThrows(IOException::class.java) { ErofsImage(MemorySource(image(29)), { true }).walk { _, _, _, _, _, _ -> } }
    }
    @Test fun rejectsRecursivePackedInode() {
        val b = fileImage(1, 1); val x = ByteBuffer.wrap(b).order(ByteOrder.LITTLE_ENDIAN)
        x.putInt(1104, 0x20); x.putLong(1120, 2); b[4231] = 0x80.toByte()
        assertThrows(IOException::class.java) { ErofsImage(MemorySource(b)).walk { _, _, _, _, _, data -> data?.invoke() } }
    }
}
