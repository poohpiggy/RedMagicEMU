package app.aemu.importer

import org.apache.commons.compress.archivers.zip.ZipArchiveEntry
import org.apache.commons.compress.archivers.zip.ZipArchiveOutputStream
import org.junit.Assert.*
import org.junit.Test
import java.io.IOException
import java.nio.file.Files
import java.io.RandomAccessFile

class TargetArchiveExtractionTest {
    private fun archive(): java.io.File {
        val file = Files.createTempFile("target-extract", ".zip").toFile()
        ZipArchiveOutputStream(file).use { out ->
            out.putArchiveEntry(ZipArchiveEntry("super.img"))
            out.write(ByteArray(2_000_000) { (it % 251).toByte() })
            out.closeArchiveEntry()
        }
        return file
    }
    private val entries = mapOf("super.img" to 2_000_000L)

    @Test fun extractsVerifiedEntryAndCleansUp() {
        val file = archive(); val dir = Files.createTempDirectory("target-output").toFile()
        try {
            TargetArchive.withExtracted(file, dir, entries, setOf("super.img"), { false }) { extracted ->
                assertEquals(2_000_000L, extracted.getValue("super.img").length())
                assertEquals(250, extracted.getValue("super.img").inputStream().use { it.skip(250); it.read() })
            }
            assertTrue(dir.listFiles()!!.isEmpty())
        } finally { file.delete(); dir.deleteRecursively() }
    }

    @Test fun cancellationCleansPartialExtraction() {
        val file = archive(); val dir = Files.createTempDirectory("target-output").toFile()
        try {
            var checks = 0
            try {
                TargetArchive.withExtracted(file, dir, entries, setOf("super.img"), { ++checks > 2 }) { fail("cancelled extraction was used") }
                fail("expected cancellation")
            } catch (_: IOException) { }
            assertTrue(dir.listFiles()!!.isEmpty())
        } finally { file.delete(); dir.deleteRecursively() }
    }

    @Test fun rejectsCrcMismatchAndCleansPartialExtraction() {
        val file = archive(); val dir = Files.createTempDirectory("target-output").toFile()
        try {
            RandomAccessFile(file, "rw").use { f ->
                val bytes = ByteArray(f.length().toInt()); f.readFully(bytes)
                val central = bytes.indices.first { i -> i + 20 < bytes.size && bytes[i] == 0x50.toByte() && bytes[i+1] == 0x4b.toByte() && bytes[i+2] == 1.toByte() && bytes[i+3] == 2.toByte() }
                f.seek((central + 16).toLong()); f.writeInt(0)
            }
            try {
                TargetArchive.withExtracted(file, dir, entries, setOf("super.img"), { false }) { fail("corrupt entry was used") }
                fail("expected CRC rejection")
            } catch (_: IOException) { }
            assertTrue(dir.listFiles()!!.isEmpty())
        } finally { file.delete(); dir.deleteRecursively() }
    }
}
