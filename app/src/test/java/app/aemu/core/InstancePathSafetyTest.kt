package app.aemu.core

import org.junit.Assert.*
import org.junit.Assume.assumeNoException
import org.junit.Test
import java.nio.file.Files
import java.nio.file.Path

class InstancePathSafetyTest {
    private fun symlink(link: Path, target: Path) {
        try { Files.createSymbolicLink(link, target) }
        catch (e: java.nio.file.FileSystemException) { assumeNoException("Symbolic link creation unavailable on this host", e) }
    }

    @Test fun acceptsOnlySafeInstanceIdAndDirectory() {
        val base = Files.createTempDirectory("images")
        try {
            assertFalse(InstancePathSafety.safeInstance(base.toFile(), "../outside"))
            Files.createDirectory(base.resolve("bcdefg"))
            assertTrue(InstancePathSafety.safeInstance(base.toFile(), "bcdefg"))
        } finally { base.toFile().deleteRecursively() }
    }

    @Test fun refusesLinkedInstanceDirectory() {
        val base = Files.createTempDirectory("images")
        val outside = Files.createTempDirectory("outside")
        try {
            symlink(base.resolve("abcdef"), outside)
            assertFalse(InstancePathSafety.safeInstance(base.toFile(), "abcdef"))
        } finally { base.toFile().deleteRecursively(); outside.toFile().deleteRecursively() }
    }

    @Test fun allowsMissingRelativePathAndRejectsTraversal() {
        val root = Files.createTempDirectory("instance")
        try {
            assertTrue(InstancePathSafety.safeRelative(root.toFile(), "root/data/missing/path"))
            assertFalse(InstancePathSafety.safeRelative(root.toFile(), "../outside"))
        } finally { root.toFile().deleteRecursively() }
    }

    @Test fun refusesLinkedParentForReset() {
        val root = Files.createTempDirectory("instance")
        val outside = Files.createTempDirectory("outside")
        try {
            Files.createDirectories(root.resolve("root/data"))
            symlink(root.resolve("root/data/misc"), outside)
            assertFalse(InstancePathSafety.safeRelative(root.toFile(), "root/data/misc/wifi"))
        } finally { root.toFile().deleteRecursively(); outside.toFile().deleteRecursively() }
    }
}
