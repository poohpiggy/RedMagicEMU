package app.aemu.importer

import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.IOException
import java.nio.ByteBuffer
import java.nio.charset.CodingErrorAction
import java.util.zip.GZIPInputStream

/** Bounded, strict gzip/newc reader for the designated package's ramdisk. */
internal object TargetRamdisk {
    fun parse(compressed: ByteArray, maxBytes: Int = 64 * 1024 * 1024, cancelled: () -> Boolean = { false }): List<BootImage.CpioEntry> {
        if (maxBytes < 0) throw IOException("Invalid ramdisk limit")
        val out = ByteArrayOutputStream()
        GZIPInputStream(ByteArrayInputStream(compressed)).use { gzip ->
            val buf = ByteArray(65536)
            while (true) {
                if (cancelled()) throw ImportCancelledException()
                val n = gzip.read(buf)
                if (n < 0) break
                if (n > maxBytes - out.size()) throw IOException("Ramdisk exceeds size limit")
                out.write(buf, 0, n)
            }
        }
        val data = out.toByteArray()
        var off = 0
        val entries = ArrayList<BootImage.CpioEntry>()
        fun align(n: Long): Long = (n + 3L) and -4L
        while (true) {
            if (cancelled()) throw ImportCancelledException()
            if (off < 0 || off.toLong() + 110 > data.size) throw IOException("Truncated ramdisk header")
            val magic = String(data, off, 6, Charsets.US_ASCII)
            if (magic != "070701" && magic != "070702") throw IOException("Invalid ramdisk header")
            fun hex(i: Int): Long {
                val s = String(data, off + 6 + i * 8, 8, Charsets.US_ASCII)
                return s.toLongOrNull(16) ?: throw IOException("Invalid ramdisk field")
            }
            val mode = hex(1).toInt()
            val size = hex(6)
            val nameSize = hex(11)
            val checksum = hex(12)
            if (nameSize !in 1..4096 || size > maxBytes) throw IOException("Invalid ramdisk entry size")
            val nameStart = off.toLong() + 110
            val nameEnd = nameStart + nameSize
            val start = align(nameEnd)
            val end = start + size
            val next = align(end)
            if (nameEnd > data.size || end > data.size || next > data.size || data[(nameEnd - 1).toInt()] != 0.toByte())
                throw IOException("Truncated ramdisk entry")
            val name = try {
                Charsets.UTF_8.newDecoder().onMalformedInput(CodingErrorAction.REPORT).onUnmappableCharacter(CodingErrorAction.REPORT)
                    .decode(ByteBuffer.wrap(data, nameStart.toInt(), nameSize.toInt() - 1)).toString()
            } catch (e: Exception) { throw IOException("Invalid ramdisk name", e) }
            if (name.indexOf('\u0000') >= 0) throw IOException("Invalid ramdisk name")
            if (name == "TRAILER!!!") {
                if (size != 0L) throw IOException("Invalid ramdisk trailer")
                break
            }
            if (entries.size >= 100_000) throw IOException("Too many ramdisk entries")
            val content = data.copyOfRange(start.toInt(), end.toInt())
            if (magic == "070702" && content.fold(0L) { sum, b -> (sum + (b.toInt() and 255)) and 0xffffffffL } != checksum)
                throw IOException("Ramdisk checksum mismatch")
            entries.add(BootImage.CpioEntry(name.removePrefix("./"), mode, content))
            off = next.toInt()
        }
        return entries
    }
}
