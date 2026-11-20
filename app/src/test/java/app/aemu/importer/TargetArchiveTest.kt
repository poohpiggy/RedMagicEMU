package app.aemu.importer

import org.apache.commons.compress.archivers.zip.ZipArchiveEntry
import org.apache.commons.compress.archivers.zip.ZipArchiveOutputStream
import org.apache.commons.compress.archivers.zip.Zip64Mode
import org.apache.commons.compress.archivers.zip.ZipFile
import org.junit.Assert.*
import org.junit.Test
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.IOException
import java.io.InputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.file.Files
import java.util.Random
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.zip.Deflater
import java.util.zip.DeflaterOutputStream

class TargetArchiveTest {
    private fun archive(entries: List<Pair<String, Int>>): File {
        val file = Files.createTempFile("target-archive", ".zip").toFile()
        ZipArchiveOutputStream(file).use { out ->
            for ((name, mode) in entries) {
                val entry = ZipArchiveEntry(name)
                entry.unixMode = mode
                out.putArchiveEntry(entry)
                out.write(byteArrayOf(1, 2, 3))
                out.closeArchiveEntry()
            }
        }
        return file
    }

    @Test fun acceptsOnlyExactExpectedEntries() {
        val file = archive(listOf("super.img" to 0x81a4, "ramdisk.img" to 0x81a4))
        try { TargetArchive.validate(file, mapOf("super.img" to 3L, "ramdisk.img" to 3L)) }
        finally { file.delete() }
    }

    @Test fun rejectsTraversalLinkExtraAndWrongSize() {
        for (entry in listOf("../super.img" to 0x81a4, "super.img" to 0xa1ff, "extra" to 0x81a4)) {
            val file = archive(listOf(entry))
            try {
                try { TargetArchive.validate(file, mapOf("super.img" to 3L)); fail("accepted $entry") }
                catch (_: IOException) { }
            } finally { file.delete() }
        }
        val file = archive(listOf("super.img" to 0x81a4))
        try {
            try { TargetArchive.validate(file, mapOf("super.img" to 4L)); fail("accepted wrong size") }
            catch (_: IOException) { }
        } finally { file.delete() }
    }

    @Test fun productionFormatAllowsDifferentEntrySizes() {
        val file = archive(listOf("super.img" to 0x81a4, "ramdisk.img" to 0x81a4, "boot.img" to 0x81a4,
            "init_boot.img" to 0x81a4, "vendor_boot.img" to 0x81a4, "README.txt" to 0x81a4))
        try { TargetArchive.validate(file) } finally { file.delete() }
    }

    @Test fun rejectsCorruptArchive() {
        val file = Files.createTempFile("bad-archive", ".zip").toFile()
        try {
            file.writeText("not a zip")
            try { TargetArchive.validate(file, mapOf("super.img" to 3L)); fail("accepted corruption") }
            catch (_: IOException) { }
        } finally { file.delete() }
    }

    @Test fun inflatesTheSuperImageInBatches() {
        val plain = ByteArray(3 * 1024 * 1024).also { Random(1).nextBytes(it) }
        val encoded = ByteArrayOutputStream()
        val deflater = Deflater(Deflater.BEST_SPEED, true)
        try {
            DeflaterOutputStream(encoded, deflater).use { it.write(plain) }
        } finally {
            deflater.end()
        }
        val secondRead = CountDownLatch(1)
        val raw = object : ByteArrayInputStream(encoded.toByteArray()) {
            var reads = 0
            override fun read(buffer: ByteArray, offset: Int, length: Int): Int {
                reads++
                if (reads == 2) secondRead.countDown()
                return super.read(buffer, offset, length)
            }
        }
        TargetArchive.inflateSuper(raw).use { input ->
            assertEquals(plain[0].toInt() and 0xff, input.read())
            assertTrue("The reader should fetch the next block while the first still has output", secondRead.await(3, TimeUnit.SECONDS))
            assertArrayEquals(plain.copyOfRange(1, plain.size), input.readBytes())
        }
        assertTrue("Compressed stream read ${raw.reads} times", raw.reads <= 3)
    }

    @Test fun prefetchFailurePropagatesAndClosesTheInput() {
        val failure = IOException("Could not read firmware")
        var closed = false
        val raw = object : InputStream() {
            override fun read(): Int = throw failure
            override fun close() { closed = true }
        }
        assertSame(failure, assertThrows(IOException::class.java) {
            TargetArchive.inflateSuper(raw).use { it.read() }
        })
        assertTrue(closed)
    }

    @Test fun extractsDeflatedZip64AndStillChecksItsCrc() {
        val work = Files.createTempDirectory("target-zip64").toFile()
        try {
            val archive = File(work, "source.zip")
            val image = ByteArray(500_000).also { Random(2).nextBytes(it) }
            ZipArchiveOutputStream(archive).use { out ->
                out.setUseZip64(Zip64Mode.Always)
                out.putArchiveEntry(ZipArchiveEntry("super.img"))
                out.write(image)
                out.closeArchiveEntry()
            }
            val entries = mapOf("super.img" to image.size.toLong())
            TargetArchive.withExtracted(archive, work, entries, entries.keys, { false }) {
                assertArrayEquals(image, it.getValue("super.img").readBytes())
            }
            val bytes = ByteBuffer.wrap(archive.readBytes()).order(ByteOrder.LITTLE_ENDIAN)
            val central = (bytes.capacity() - 46 downTo 0).first { bytes.getInt(it) == 0x02014b50 }
            bytes.putInt(central + 16, bytes.getInt(central + 16) xor 1)
            archive.writeBytes(bytes.array())
            assertThrows(IOException::class.java) {
                archive.inputStream().use { input ->
                    TargetArchive.withExtracted(ZipFile.builder().setSeekableByteChannel(input.channel),
                        work, entries, entries.keys, { false }) {
                        fail("Published firmware despite a CRC mismatch")
                    }
                }
            }
        } finally {
            work.deleteRecursively()
        }
    }
    private fun productionArchive(zteSize: Int?, zteMode: Int = 0x81a4): File {
        val file = Files.createTempFile("target-ztecfg", ".zip").toFile()
        ZipArchiveOutputStream(file).use { out ->
            for (name in listOf("super.img", "ramdisk.img", "boot.img", "init_boot.img", "vendor_boot.img", "README.txt")) {
                out.putArchiveEntry(ZipArchiveEntry(name)); out.write(1); out.closeArchiveEntry()
            }
            if (zteSize != null) {
                val entry = ZipArchiveEntry("ztecfg.img"); entry.unixMode = zteMode
                out.putArchiveEntry(entry); out.write(ByteArray(zteSize)); out.closeArchiveEntry()
            }
        }
        return file
    }

    @Test fun acceptsOptionalZtecfgWithExactSize() {
        for (size in listOf(null, 524288)) {
            val file = productionArchive(size)
            try { TargetArchive.validate(file) } finally { file.delete() }
        }
    }

    @Test fun rejectsZtecfgWrongSizeAndLink() {
        for ((size, mode) in listOf(524287 to 0x81a4, 524289 to 0x81a4, 524288 to 0xa1ff)) {
            val file = productionArchive(size, mode)
            try {
                try { TargetArchive.validate(file); fail("accepted invalid ztecfg") }
                catch (_: IOException) { }
            } finally { file.delete() }
        }
    }

    @Test fun rejectsDuplicateZtecfg() {
        val file = Files.createTempFile("target-duplicate", ".zip").toFile()
        try {
            ZipArchiveOutputStream(file).use { out ->
                for (name in listOf("super.img", "ramdisk.img", "boot.img", "init_boot.img", "vendor_boot.img", "README.txt", "ztecfg.img", "ztecfg.img")) {
                    out.putArchiveEntry(ZipArchiveEntry(name))
                    out.write(if (name == "ztecfg.img") ByteArray(524288) else byteArrayOf(1))
                    out.closeArchiveEntry()
                }
            }
            try { TargetArchive.validate(file); fail("accepted duplicate ztecfg") }
            catch (_: IOException) { }
        } finally { file.delete() }
    }

}
