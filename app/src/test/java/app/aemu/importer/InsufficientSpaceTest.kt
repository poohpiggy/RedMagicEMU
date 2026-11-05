package app.aemu.importer

import org.apache.commons.compress.archivers.zip.ZipArchiveEntry
import org.apache.commons.compress.archivers.zip.ZipArchiveOutputStream
import org.junit.Assert.*
import org.junit.Test
import java.io.ByteArrayInputStream
import java.io.File
import java.io.IOException
import java.nio.file.Files

class InsufficientSpaceTest {
    private fun archive(parent: File): File = File(parent, "input.zip").also { file ->
        ZipArchiveOutputStream(file).use { zip ->
            zip.putArchiveEntry(ZipArchiveEntry("super.img"))
            zip.write(byteArrayOf(1, 2, 3))
            zip.closeArchiveEntry()
        }
    }

    @Test fun insufficientSpaceBeforeUnpackingPreservesPublishedDataAndCleansLease() {
        val root = Files.createTempDirectory("space-test").toFile()
        try {
            val images = File(root, "images").apply { mkdir() }
            val old = File(images, "abcdef").apply { mkdir() }.resolve("image.json").apply { writeText("keep") }
            val zip = archive(root)
            try {
                PendingImports.begin(images).use { lease ->
                    TargetArchive.withExtracted(zip, lease.dir, mapOf("super.img" to 3L), setOf("super.img"),
                        { false }, availableBytes = { 2L }) { fail("unpacking must not start") }
                }
                fail("expected insufficient space")
            } catch (e: IOException) { assertTrue(e.message!!.contains("space")) }
            assertEquals("keep", old.readText())
            assertEquals(listOf(".management.lock"), File(images, ".firmware-pending").list()!!.sorted())
        } finally { root.deleteRecursively() }
    }

    @Test fun spaceRunningOutDuringTreeCreationCleansIncompleteLease() {
        val root = Files.createTempDirectory("space-test").toFile()
        try {
            val images = File(root, "images").apply { mkdir() }
            val old = File(images, "abcdef").apply { mkdir() }.resolve("image.json").apply { writeText("keep") }
            try {
                PendingImports.begin(images).use { lease ->
                    val tree = File(lease.dir, "root").apply { mkdir() }
                    var calls = 0
                    val writer = SafeTreeWriter(tree, 10, 100, { false }, availableBytes = { if (++calls == 1) 100L else 0L })
                    writer.write("system/first", 3, ByteArrayInputStream(byteArrayOf(1, 2, 3)))
                    writer.write("system/second", 3, ByteArrayInputStream(byteArrayOf(4, 5, 6)))
                }
                fail("expected insufficient space")
            } catch (e: IOException) { assertTrue(e.message!!.contains("space")) }
            assertEquals("keep", old.readText())
            assertEquals(listOf(".management.lock"), File(images, ".firmware-pending").list()!!.sorted())
        } finally { root.deleteRecursively() }
    }
}
