package app.aemu.core

import org.junit.Assert.*
import org.junit.Assume.assumeNoException
import org.junit.Test
import java.io.File
import java.nio.file.Files

class CloneSafetyTest {
    @Test fun acceptsSystemWithinGuestRoot() {
        val root = Files.createTempDirectory("clone-root").toFile()
        try {
            val system = File(root, "system").apply { mkdir() }
            assertEquals(system.canonicalFile, CloneSafety.systemDirectory(root))
        } finally { root.deleteRecursively() }
    }

    @Test fun refusesSystemLinkOutsideGuestRoot() {
        val root = Files.createTempDirectory("clone-root").toFile()
        val outside = Files.createTempDirectory("clone-outside").toFile()
        try {
            try { Files.createSymbolicLink(File(root, "system").toPath(), outside.toPath()) }
            catch (e: java.nio.file.FileSystemException) { assumeNoException(e) }
            assertTrue(runCatching { CloneSafety.systemDirectory(root) }.isFailure)
        } finally { root.deleteRecursively(); outside.deleteRecursively() }
    }
}
