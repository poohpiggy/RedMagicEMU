package app.aemu.importer

import org.apache.commons.compress.archivers.zip.ZipArchiveEntry
import org.apache.commons.compress.archivers.zip.ZipArchiveOutputStream
import org.apache.commons.compress.archivers.zip.ZipFile
import org.junit.Assert.*
import org.junit.Test
import java.io.File
import java.io.IOException
import java.nio.file.Files
import java.io.RandomAccessFile
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.Random
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean

class TargetArchiveExtractionTest {
    private val blockSize = 1024 * 1024
    private val treeEntries = mapOf("ramdisk.img" to 1L, "super.img" to 3L * blockSize)

    private fun treeArchive(work: File): File = File(work, "firmware.zip").also { file ->
        ZipArchiveOutputStream(file).use { zip ->
            zip.putArchiveEntry(ZipArchiveEntry("ramdisk.img"))
            zip.write(1)
            zip.closeArchiveEntry()
            zip.putArchiveEntry(ZipArchiveEntry("super.img"))
            zip.write(ByteArray(3 * blockSize).also { it[0] = 7 })
            zip.closeArchiveEntry()
        }
    }

    @Test fun buildsFromWrittenPrefixesAndPublishesTheResultOnlyAfterVerification() {
        val work = Files.createTempDirectory("target-tree-overlap").toFile()
        try {
            val root = work.resolve("root").apply { mkdir() }
            val scratch = work.resolve("scratch").apply { mkdir() }
            val file = treeArchive(work)
            val started = CountDownLatch(1)
            val readLast = AtomicBoolean()
            val tree = PendingPackageTree(root, { false }) { source, _, _, _ ->
                val first = ByteBuffer.allocate(1)
                source.read(0, first)
                assertEquals(7.toByte(), first.get(0))
                started.countDown()
                val last = ByteBuffer.allocate(1)
                source.read(source.size - 1, last)
                assertEquals(0.toByte(), last.get(0))
                readLast.set(true)
                emptyList()
            }
            TargetArchive.withExtracted(ZipFile.builder().setFile(file), scratch, treeEntries, treeEntries.keys,
                { false }, prepareTree = tree, onBytesExtracted = { copied, _ ->
                    if (copied == 1L + blockSize) {
                        assertTrue(started.await(5, TimeUnit.SECONDS))
                        assertFalse(readLast.get())
                        assertThrows(IOException::class.java) { tree.awaitVerified() }
                    }
                }) {
                assertTrue(tree.awaitVerified().isEmpty())
                assertTrue(readLast.get())
            }
            assertTrue(scratch.listFiles()!!.isEmpty())
        } finally { work.deleteRecursively() }
    }

    @Test fun checksumFailureAndCancellationJoinTheWaitingTreeReader() {
        for (cancel in listOf(false, true)) {
            val work = Files.createTempDirectory("discard-overlap").toFile()
            try {
                val root = work.resolve("root").apply { mkdir() }
                val scratch = work.resolve("scratch").apply { mkdir() }
                val file = treeArchive(work)
                if (!cancel) {
                    val data = file.readBytes()
                    val buffer = ByteBuffer.wrap(data).order(ByteOrder.LITTLE_ENDIAN)
                    val central = (data.size - 55 downTo 0).first { index ->
                        buffer.getInt(index) == 0x02014b50 && String(data, index + 46, 9) == "super.img"
                    }
                    buffer.putInt(central + 16, buffer.getInt(central + 16) xor 1)
                    file.writeBytes(data)
                }
                val cancelled = AtomicBoolean()
                val started = CountDownLatch(1)
                val stopped = AtomicBoolean()
                val tree = PendingPackageTree(root, cancelled::get) { source, _, _, _ ->
                    started.countDown()
                    try { source.read(source.size - 1, ByteBuffer.allocate(1)) }
                    finally { stopped.set(true) }
                    emptyList()
                }
                val error = assertThrows(IOException::class.java) {
                    TargetArchive.withExtracted(ZipFile.builder().setFile(file), scratch, treeEntries, treeEntries.keys,
                        cancelled::get, prepareTree = tree, onBytesExtracted = { copied, _ ->
                            if (copied == 1L + blockSize) {
                                assertTrue(started.await(5, TimeUnit.SECONDS))
                                if (cancel) cancelled.set(true)
                            }
                        }) { fail("Unverified firmware was returned") }
                }
                if (cancel) assertTrue(error is ImportCancelledException)
                else assertTrue(error.message.orEmpty().contains("corrupt"))
                assertTrue(stopped.get())
                assertTrue(scratch.listFiles()!!.isEmpty())
            } finally { work.deleteRecursively() }
        }
    }

    @Test fun extractsOptionalZtecfgOnlyWhenPresent() {
        for (include in listOf(false, true)) {
            val file = Files.createTempFile("ztecfg-archive", ".zip").toFile()
            val dir = Files.createTempDirectory("ztecfg-output").toFile()
            try {
                ZipArchiveOutputStream(file).use { out ->
                    for ((name, bytes) in listOf("super.img" to byteArrayOf(7)) +
                        if (include) listOf("ztecfg.img" to ByteArray(524288) { 9 }) else emptyList()) {
                        out.putArchiveEntry(ZipArchiveEntry(name)); out.write(bytes); out.closeArchiveEntry()
                    }
                }
                val entries = if (include) mapOf("super.img" to 1L, "ztecfg.img" to 524288L) else mapOf("super.img" to 1L)
                TargetArchive.withExtracted(file, dir, entries, setOf("super.img"), { false }, optionalNames = setOf("ztecfg.img")) { extracted ->
                    assertEquals(include, extracted.containsKey("ztecfg.img"))
                    if (include) assertEquals(9, extracted.getValue("ztecfg.img").inputStream().use { it.read() })
                }
            } finally { file.delete(); dir.deleteRecursively() }
        }
    }
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

    @Test fun superImagePreservesDataAcrossZeroRangesAndExactEnd() {
        val dir = Files.createTempDirectory("target-zero-ranges").toFile()
        try {
            val file = File(dir, "firmware.zip")
            val chunk = 1024 * 1024
            val image = ByteArray(4 * chunk + 127)
            val random = ByteArray(chunk).also { Random(3).nextBytes(it) }
            random.copyInto(image)
            random.copyInto(image, 2 * chunk)
            ZipArchiveOutputStream(file).use { out ->
                out.putArchiveEntry(ZipArchiveEntry("super.img"))
                out.write(image)
                out.closeArchiveEntry()
            }
            val expected = mapOf("super.img" to image.size.toLong())
            val copied = mutableListOf<Long>()
            TargetArchive.withExtracted(ZipFile.builder().setFile(file), dir, expected, expected.keys, { false },
                onBytesExtracted = { bytes, _ -> copied.add(bytes) }) { extracted ->
                val unpacked = extracted.getValue("super.img")
                assertEquals(image.size.toLong(), unpacked.length())
                assertArrayEquals(image, unpacked.readBytes())
            }
            assertEquals(listOf(0L, chunk.toLong(), 2L * chunk, 3L * chunk, 4L * chunk, image.size.toLong()), copied)
        } finally { dir.deleteRecursively() }
    }

    @Test fun extractsFromAnOpenFileWithByteProgress() {
        val file = archive()
        val dir = Files.createTempDirectory("target-direct-output").toFile()
        try {
            val progress = mutableListOf<Long>()
            file.inputStream().use { input ->
                TargetArchive.withExtracted(
                    ZipFile.builder().setSeekableByteChannel(input.channel), dir, entries, entries.keys, { false },
                    onBytesExtracted = { copied, total ->
                        assertEquals(2_000_000L, total)
                        progress.add(copied)
                    },
                ) { extracted ->
                    assertEquals(2_000_000L, extracted.getValue("super.img").length())
                    assertEquals(250, extracted.getValue("super.img").inputStream().use { it.skip(250); it.read() })
                }
            }
            assertEquals(0L, progress.first())
            assertEquals(2_000_000L, progress.last())
            assertTrue(progress.size >= 3)
            assertTrue(progress.zipWithNext().all { (before, after) -> before < after })
            assertTrue(dir.listFiles()!!.isEmpty())
        } finally { file.delete(); dir.deleteRecursively() }
    }

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
            val cancelled = AtomicBoolean()
            assertThrows(ImportCancelledException::class.java) {
                TargetArchive.withExtracted(ZipFile.builder().setFile(file), dir, entries, entries.keys, cancelled::get,
                    onBytesExtracted = { bytes, _ -> if (bytes > 0) cancelled.set(true) }) {
                    fail("cancelled extraction was used")
                }
            }
            assertTrue(cancelled.get())
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
