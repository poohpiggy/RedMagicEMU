package app.aemu.importer

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import java.io.IOException
import java.io.InputStream
import java.nio.ByteBuffer
import java.nio.channels.FileChannel
import java.nio.file.Path
import java.nio.file.StandardOpenOption.CREATE_NEW
import java.nio.file.StandardOpenOption.SPARSE
import java.nio.file.StandardOpenOption.WRITE
import java.util.zip.CRC32

/** Overlap reading and verification with writing the private, logically complete super image. */
internal object SuperImageExtraction {
    private const val BLOCK_SIZE = 1024 * 1024

    fun write(
        input: InputStream,
        path: Path,
        size: Long,
        expectedCrc: Long,
        cancelled: () -> Boolean,
        onBytesWritten: (Long) -> Unit,
        onPrefixAvailable: ((Long) -> Unit)? = null,
    ) {
        FileChannel.open(path, WRITE, CREATE_NEW, SPARSE).use { output ->
            runBlocking {
                val available = Channel<Block>(2)
                repeat(2) { available.send(Block()) }
                val ready = Channel<Block>()
                launch(Dispatchers.Default) {
                    val crc = CRC32()
                    var decoded = 0L
                    while (true) {
                        val block = available.receive()
                        block.size = readBlock(input, block.bytes, size - decoded, cancelled)
                        if (block.size == 0) break
                        crc.update(block.bytes, 0, block.size)
                        decoded += block.size
                        ready.send(block)
                    }
                    if (decoded != size || crc.value != expectedCrc) throw IOException("Firmware entry is corrupt: super.img")
                    ready.close()
                }

                val zeros = ByteArray(BLOCK_SIZE)
                var written = 0L
                var outputPosition = 0L
                for (block in ready) {
                    if (cancelled()) throw ImportCancelledException()
                    if (!block.isAllZero(zeros)) {
                        if (outputPosition != written) output.position(written)
                        block.buffer.clear().limit(block.size)
                        writeFully(output, block.buffer)
                        outputPosition = written + block.size
                        onPrefixAvailable?.invoke(outputPosition)
                    }
                    written += block.size
                    onBytesWritten(written)
                    available.send(block)
                }
                if (written > outputPosition) {
                    // Seeking alone cannot set the length when the last blocks are all zero.
                    output.position(written - 1)
                    writeFully(output, ByteBuffer.wrap(byteArrayOf(0)))
                    onPrefixAvailable?.invoke(written)
                }
            }
        }
    }

    private fun readBlock(input: InputStream, bytes: ByteArray, remaining: Long, cancelled: () -> Boolean): Int {
        var size = 0
        while (size < bytes.size) {
            if (cancelled()) throw ImportCancelledException()
            val n = input.read(bytes, size, bytes.size - size)
            if (n < 0) break
            if (n.toLong() > remaining - size) throw IOException("Firmware entry exceeds declared size: super.img")
            size += n
        }
        return size
    }

    private fun writeFully(output: FileChannel, buffer: ByteBuffer) {
        while (buffer.hasRemaining()) {
            if (output.write(buffer) <= 0) throw IOException("Cannot write extracted super.img")
        }
    }

    private class Block {
        val bytes = ByteArray(BLOCK_SIZE)
        val buffer: ByteBuffer = ByteBuffer.wrap(bytes)
        var size = 0

        fun isAllZero(zeros: ByteArray): Boolean {
            if (size == bytes.size) return bytes.contentEquals(zeros)
            for (i in 0 until size) if (bytes[i] != 0.toByte()) return false
            return true
        }
    }
}
