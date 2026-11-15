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
