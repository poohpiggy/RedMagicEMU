package app.aemu.importer
import org.junit.Test
import org.junit.Assert.*
import java.io.IOException
import java.nio.file.Files

class TargetPackageTreeTest {
    @Test fun invalidSuperIsRejectedAndNoEntriesExtracted() {
        val dir = Files.createTempDirectory("target-package").toFile()
        try {
            val superFile = dir.resolve("super.img").apply { writeBytes(ByteArray(16384)) }
            val ramdisk = dir.resolve("ramdisk.img").apply { writeBytes(ByteArray(4)) }
            val root = dir.resolve("root").apply { mkdir() }
            try { TargetPackageTree.extract(superFile, ramdisk, root, { false }); fail() } catch (_: IOException) { }
            assertEquals(0, root.listFiles()!!.size)
        } finally { dir.deleteRecursively() }
    }
}
