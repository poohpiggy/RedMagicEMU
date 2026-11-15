package app.aemu.importer

import android.content.Context
import app.aemu.core.EngineAssets
import java.io.ByteArrayOutputStream
import java.io.IOException
import java.io.InputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.security.MessageDigest
import java.util.zip.CRC32
import java.util.zip.DataFormatException
import java.util.zip.Inflater

/** Decodes only the approved, metadata-free, unfiltered 8-bit grayscale resource. */
internal object TargetAodMaskResource {
    private const val ASSET = "engines/redmagic/aod_theme64_alpha.png"
    private const val PNG_SHA = "1aafc69b321aa880471e7f16bedba07f644608f678c512d5b732564f7a314849"
    private const val MASK_SHA = "019bfeea5f8e5109befa231f8febce04732320ff1e256e51fa28b092e6231c7a"
    private const val WIDTH = 1216
    private const val HEIGHT = 2688
    private const val MAX_PNG = 128 * 1024
    private val signature = byteArrayOf(0x89.toByte(), 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a)

    /** The build's indexed EngineAssets entry is the only production source. */
    fun readAsset(ctx: Context): ByteArray = EngineAssets.open(ctx, ASSET).use { decode(it) }

    fun decode(input: InputStream): ByteArray {
        val all = ByteArrayOutputStream()
        val buffer = ByteArray(8192)
        while (true) {
            val n = input.read(buffer)
            if (n < 0) break
            if (n == 0) continue
            if (n > MAX_PNG - all.size()) throw IOException("Oversized AOD mask resource")
            all.write(buffer, 0, n)
        }
        val png = all.toByteArray()
        if (!matches(png, PNG_SHA)) throw IOException("AOD mask resource digest mismatch")
        if (png.size < 8 || !png.copyOfRange(0, 8).contentEquals(signature)) throw IOException("Invalid AOD PNG")
        val cursor = ByteBuffer.wrap(png).order(ByteOrder.BIG_ENDIAN)
        cursor.position(8)
        fun chunk(expected: String): ByteArray {
            if (cursor.remaining() < 12) throw IOException("Truncated AOD PNG")
            val size = cursor.int
            if (size < 0 || size > cursor.remaining() - 8) throw IOException("Invalid AOD PNG chunk length")
            val kind = ByteArray(4).also { cursor.get(it) }
            if (String(kind, Charsets.US_ASCII) != expected) throw IOException("Unexpected AOD PNG chunk")
            val payload = ByteArray(size).also { cursor.get(it) }
            val stored = cursor.int.toLong() and 0xffffffffL
            val crc = CRC32().apply { update(kind); update(payload) }.value
            if (crc != stored) throw IOException("AOD PNG checksum mismatch")
            return payload
        }
        val header = chunk("IHDR")
        val expectedHeader = ByteBuffer.allocate(13).order(ByteOrder.BIG_ENDIAN)
            .putInt(WIDTH).putInt(HEIGHT).put(8).put(0).put(0).put(0).put(0).array()
        if (!header.contentEquals(expectedHeader)) throw IOException("Invalid AOD PNG dimensions or encoding")
        val compressed = chunk("IDAT")
        if (chunk("IEND").isNotEmpty() || cursor.hasRemaining()) throw IOException("Unexpected AOD PNG data")
        val rows = ByteArray((WIDTH + 1) * HEIGHT + 1)
        val inflater = Inflater()
        val count: Int
        try {
            inflater.setInput(compressed)
            var written = 0
            while (!inflater.finished() && written < rows.size) {
                val n = inflater.inflate(rows, written, rows.size - written)
                if (n == 0) break
                written += n
            }
            if (!inflater.finished() || inflater.remaining != 0 || written != rows.size - 1)
                throw IOException("Invalid AOD PNG compressed data")
            count = written
        } catch (e: DataFormatException) { throw IOException("Invalid AOD PNG compressed data", e) }
        finally { inflater.end() }
        if (count != (WIDTH + 1) * HEIGHT) throw IOException("Invalid AOD PNG length")
        val mask = ByteArray(WIDTH * HEIGHT)
        for (y in 0 until HEIGHT) {
            val row = y * (WIDTH + 1)
            if (rows[row] != 0.toByte()) throw IOException("Unsupported AOD PNG filter")
            System.arraycopy(rows, row + 1, mask, y * WIDTH, WIDTH)
        }
        if (!matches(mask, MASK_SHA)) throw IOException("AOD mask pixel digest mismatch")
        return mask
    }

    private fun matches(data: ByteArray, expected: String): Boolean {
        val digest = MessageDigest.getInstance("SHA-256").digest(data)
        val required = ByteArray(32) { expected.substring(it * 2, it * 2 + 2).toInt(16).toByte() }
        return MessageDigest.isEqual(digest, required)
    }
}
