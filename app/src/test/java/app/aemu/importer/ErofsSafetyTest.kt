package app.aemu.importer

import org.junit.Assert.*
import org.junit.Test
import java.io.IOException
import java.nio.ByteBuffer
import java.nio.ByteOrder

class ErofsSafetyTest {
    private class MemorySource(val bytes: ByteArray) : RandomSource {
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
