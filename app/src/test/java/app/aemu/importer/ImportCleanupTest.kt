package app.aemu.importer
import org.junit.Assert.*
import org.junit.Test
import java.nio.file.Files

class ImportCleanupTest {
    @Test fun removesIncompletePrivateTree() {
        val parent = Files.createTempDirectory("import-cleanup").toFile()
        try {
            val child = parent.resolve("instance").apply { mkdir() }
            child.resolve("root/sub").mkdirs()
            child.resolve("root/sub/file").writeText("temporary")
            ImportCleanup.remove(child)
            assertFalse(child.exists())
        } finally { parent.deleteRecursively() }
    }
    @Test fun doesNotFollowLinksOutsideTheInstance() {
        val parent = Files.createTempDirectory("import-cleanup")
        try {
            val outside = Files.createDirectory(parent.resolve("outside"))
            val marker = Files.write(outside.resolve("marker"), byteArrayOf(1))
            val child = Files.createDirectory(parent.resolve("instance"))
            try { Files.createSymbolicLink(child.resolve("link"), outside) }
            catch (e: java.nio.file.FileSystemException) { org.junit.Assume.assumeNoException(e) }
            ImportCleanup.remove(child.toFile())
            assertTrue(Files.exists(marker))
        } finally { parent.toFile().deleteRecursively() }
    }

}
