package app.aemu.importer

import org.junit.Assert.*
import org.junit.Test
import java.io.IOException
import java.nio.ByteBuffer
import java.nio.channels.FileChannel
import java.nio.file.Files
import java.nio.file.StandardOpenOption

class StrictChannelSourceTest {
    @Test fun readsWithinBoundsAndRejectsOutside() {
        val file = Files.createTempFile("strict-source", ".bin")
        try {
            Files.write(file, byteArrayOf(1, 2, 3))
            FileChannel.open(file, StandardOpenOption.READ).use { ch ->
                val src = StrictChannelSource(ch)
                val dst = ByteBuffer.allocate(2); src.read(1, dst)
                assertArrayEquals(byteArrayOf(2, 3), dst.array())
                assertThrows(IOException::class.java) { src.read(2, ByteBuffer.allocate(2)) }
                assertThrows(IOException::class.java) { src.read(-1, ByteBuffer.allocate(1)) }
            }
        } finally { Files.deleteIfExists(file) }
    }
    @Test fun rejectsTruncationAfterOpening() {
        val file = Files.createTempFile("strict-source", ".bin")
        try {
            Files.write(file, byteArrayOf(1, 2, 3))
            FileChannel.open(file, StandardOpenOption.READ, StandardOpenOption.WRITE).use { ch ->
                val src = StrictChannelSource(ch); ch.truncate(1)
                assertThrows(IOException::class.java) { src.read(0, ByteBuffer.allocate(3)) }
            }
        } finally { Files.deleteIfExists(file) }
    }
}
