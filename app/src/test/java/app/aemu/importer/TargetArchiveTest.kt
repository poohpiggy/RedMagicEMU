package app.aemu.importer

import org.apache.commons.compress.archivers.zip.ZipArchiveEntry
import org.apache.commons.compress.archivers.zip.ZipArchiveOutputStream
import org.junit.Assert.*
import org.junit.Test
import java.io.File
import java.io.IOException
import java.nio.file.Files

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
}
