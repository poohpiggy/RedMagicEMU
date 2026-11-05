package app.aemu.importer

import org.junit.Assert.*
import org.junit.Test
import java.io.ByteArrayOutputStream
import java.io.IOException
import java.util.zip.GZIPOutputStream

class TargetRamdiskTest {
    private fun cpio(name: String, data: ByteArray): ByteArray {
        val out = ByteArrayOutputStream()
        fun entry(n: String, contents: ByteArray) {
            val fields = longArrayOf(1, 0x81a4, 0, 0, 1, 0, contents.size.toLong(), 0, 0, 0, 0, n.toByteArray().size.toLong() + 1, 0)
            out.write(("070701" + fields.joinToString("") { "%08x".format(it) }).toByteArray())
            out.write(n.toByteArray()); out.write(0)
            while (out.size() % 4 != 0) out.write(0)
            out.write(contents)
            while (out.size() % 4 != 0) out.write(0)
        }
        entry(name, data); entry("TRAILER!!!", byteArrayOf())
        return out.toByteArray()
    }
    private fun gzip(data: ByteArray): ByteArray = ByteArrayOutputStream().also { out -> GZIPOutputStream(out).use { it.write(data) } }.toByteArray()
    @Test fun readsValidGzipCpio() {
        val entries = TargetRamdisk.parse(gzip(cpio("test/file", byteArrayOf(1,2,3))))
        assertEquals("test/file", entries.single().name)
        assertArrayEquals(byteArrayOf(1,2,3), entries.single().data)
    }
    @Test fun rejectsTruncatedGzip() {
        val valid = gzip(cpio("x", byteArrayOf(1)))
        try { TargetRamdisk.parse(valid.copyOf(valid.size - 3)); fail() } catch (_: IOException) { }
    }
    @Test fun rejectsMissingTrailer() {
        val valid = cpio("x", byteArrayOf(1))
        try { TargetRamdisk.parse(gzip(valid.copyOf(112))); fail() } catch (_: IOException) { }
    }
    @Test fun enforcesExpandedLimit() {
        try { TargetRamdisk.parse(gzip(cpio("x", ByteArray(100))), maxBytes = 64); fail() } catch (_: IOException) { }
    }
    @Test fun honoursCancellation() {
        try { TargetRamdisk.parse(gzip(cpio("x", byteArrayOf())), cancelled = { true }); fail() } catch (_: IOException) { }
    }
}
