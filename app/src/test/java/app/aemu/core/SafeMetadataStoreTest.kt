package app.aemu.core

import java.io.IOException
import java.nio.file.Files
import org.junit.Assert.*
import org.junit.Test

class SafeMetadataStoreTest {
    @Test fun replacesExistingMetadataAtomically() {
        val dir = Files.createTempDirectory("metadata").toFile()
        try {
            val file = dir.resolve("image.json")
            file.writeText("old")
            SafeMetadataStore.write(dir, "new".toByteArray())
            assertEquals("new", file.readText())
            assertEquals(listOf("image.json"), dir.list()!!.sorted())
        } finally { dir.deleteRecursively() }
    }

    @Test fun rejectsDirectoryAtMetadataDestination() {
        val dir = Files.createTempDirectory("metadata").toFile()
        try {
            val target = dir.resolve("image.json").apply { mkdir() }
            target.resolve("keep").writeText("untouched")
            assertThrows(IOException::class.java) { SafeMetadataStore.write(dir, "new".toByteArray()) }
            assertEquals("untouched", target.resolve("keep").readText())
        } finally { dir.deleteRecursively() }
    }

    @Test fun doesNotFollowOrOverwriteExistingTemporaryLink() {
        val dir = Files.createTempDirectory("metadata").toFile()
        val outside = Files.createTempFile("outside", ".txt").toFile()
        try {
            outside.writeText("untouched")
            val link = dir.resolve("image.json.tmp").toPath()
            try { Files.createSymbolicLink(link, outside.toPath()) }
            catch (e: java.nio.file.FileSystemException) { org.junit.Assume.assumeNoException(e) }
            SafeMetadataStore.write(dir, "new".toByteArray())
            assertEquals("untouched", outside.readText())
            assertTrue(Files.isSymbolicLink(link))
            assertEquals("new", dir.resolve("image.json").readText())
        } finally { dir.deleteRecursively(); outside.delete() }
    }

    @Test fun refusesSymbolicLinkAtMetadataDestination() {
        val dir = Files.createTempDirectory("metadata").toFile()
        val outside = Files.createTempFile("outside", ".txt").toFile()
        try {
            outside.writeText("untouched")
            val link = dir.resolve("image.json").toPath()
            try { Files.createSymbolicLink(link, outside.toPath()) }
            catch (e: java.nio.file.FileSystemException) { org.junit.Assume.assumeNoException(e) }
            assertThrows(IOException::class.java) { SafeMetadataStore.write(dir, "new".toByteArray()) }
            assertEquals("untouched", outside.readText())
            assertTrue(Files.isSymbolicLink(link))
        } finally { dir.deleteRecursively(); outside.delete() }
    }
}
