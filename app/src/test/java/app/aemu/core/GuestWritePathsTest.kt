package app.aemu.core

import org.junit.Assert.*
import org.junit.Test
import java.nio.file.Files

class GuestWritePathsTest {
    @Test fun acceptsOrdinaryGuestDirectoriesAndFrp() {
        val root = Files.createTempDirectory("guest-paths").toFile()
        try {
            root.resolve("default.prop").writeText("ro.frp.pst=/dev/block/frp\n")
            GuestWritePaths.requireSafe(root, listOf("/data/misc", "vendor/etc", "/dev/socket"))
        } finally { root.deleteRecursively() }
    }
    @Test fun rejectsTraversalInDirectoriesAndFrp() {
        val root = Files.createTempDirectory("guest-paths").toFile()
        try {
            assertTrue(runCatching { GuestWritePaths.requireSafe(root, listOf("/data/../../outside")) }.isFailure)
            root.resolve("default.prop").writeText("ro.frp.pst=/dev/../../outside\n")
            assertTrue(runCatching { GuestWritePaths.requireSafe(root, emptyList()) }.isFailure)
        } finally { root.deleteRecursively() }
    }
    @Test fun rejectsExistingLinkEscapingGuestRoot() {
        val root = Files.createTempDirectory("guest-paths").toFile()
        val outside = Files.createTempDirectory("outside").toFile()
        try {
            try { Files.createSymbolicLink(root.resolve("data").toPath(), outside.toPath()) }
            catch (_: Exception) { org.junit.Assume.assumeTrue("symbolic links unavailable", false) }
            assertTrue(runCatching { GuestWritePaths.requireSafe(root, listOf("/data/misc")) }.isFailure)
        } finally {
            if (Files.isSymbolicLink(root.resolve("data").toPath())) Files.delete(root.resolve("data").toPath())
            root.deleteRecursively(); outside.deleteRecursively()
        }
    }
    @Test fun acceptsAbsoluteLinkOnlyWhenItStaysInsideGuestRoot() {
        val root = Files.createTempDirectory("guest-paths").toFile()
        try {
            root.resolve("data/real").mkdirs()
            try { Files.createSymbolicLink(root.resolve("inside").toPath(), root.resolve("data/real").toPath().toAbsolutePath()) }
            catch (_: Exception) { org.junit.Assume.assumeTrue("symbolic links unavailable", false) }
            GuestWritePaths.requireSafe(root, listOf("/inside/cache"))
        } finally {
            if (Files.isSymbolicLink(root.resolve("inside").toPath())) Files.delete(root.resolve("inside").toPath())
            root.deleteRecursively()
        }
    }
}
