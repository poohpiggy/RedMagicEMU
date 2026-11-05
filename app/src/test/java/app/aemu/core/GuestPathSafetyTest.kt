package app.aemu.core

import org.junit.Assert.*
import org.junit.Test
import java.nio.file.Files
import java.nio.file.Paths

class GuestPathSafetyTest {
    @Test fun rejectsEscapeThroughExistingLink() {
        val root = Files.createTempDirectory("guest-link").toFile()
        val outside = Files.createTempDirectory("guest-outside").toFile()
        try {
            root.resolve("apex/com.example").mkdirs()
            try {
                Files.createSymbolicLink(root.resolve("external").toPath(), outside.toPath())
            } catch (e: java.nio.file.FileSystemException) { org.junit.Assume.assumeNoException(e) }
            assertFalse(GuestPathSafety.contained(root, "apex/com.example/lib", "../../external/file"))
        } finally { root.deleteRecursively(); outside.deleteRecursively() }
    }
    @Test fun allowsInternalTargetsAndMissingFinalPaths() {
        val root = Files.createTempDirectory("guest-link").toFile()
        try {
            root.resolve("system/bin").mkdirs()
            root.resolve("apex/com.example").mkdirs()
            assertTrue(GuestPathSafety.contained(root, "apex/com.example/link", "../../system/bin/sh"))
            assertFalse(GuestPathSafety.contained(root, "apex/com.example/link", "../../data/local/tmp"))
        } finally { root.deleteRecursively() }
    }
    @Test fun prospectiveLinkRejectsEscapeThroughExistingTreeLink() {
        val root = Files.createTempDirectory("guest-link").toFile()
        val outside = Files.createTempDirectory("guest-outside").toFile()
        try {
            root.resolve("system").mkdirs()
            try { Files.createSymbolicLink(root.resolve("system/escape").toPath(), outside.toPath()) }
            catch (e: java.nio.file.FileSystemException) { org.junit.Assume.assumeNoException(e) }
            assertFalse(GuestPathSafety.prospective(root, "apex/com.example/lib", "../../system/escape/file", emptyMap(), emptySet()))
        } finally { root.deleteRecursively(); outside.deleteRecursively() }
    }

    @Test fun prospectiveResolvesPlannedPartitionMoveWithoutFollowingOldLink() {
        val root = Files.createTempDirectory("guest-move").toFile()
        try {
            root.resolve("system/system_ext/lib64").mkdirs()
            root.resolve("system/system_ext/lib64/library.so").writeText("content")
            val virtual = mapOf("system/system_ext" to "../system_ext")
            assertTrue(GuestPathSafety.prospective(root, "apex/com.example/link", "/system/system_ext/lib64/library.so", virtual, setOf("system_ext")))
        } finally { root.deleteRecursively() }
    }

    @Test fun prospectiveCombinesIndividuallySafeGraphs() {
        val root = Files.createTempDirectory("guest-link").toFile()
        try {
            root.resolve("system").mkdirs()
            try { Files.createSymbolicLink(root.resolve("system/very").toPath(), Paths.get("..")) }
            catch (e: java.nio.file.FileSystemException) { org.junit.Assume.assumeNoException(e) }
            val target = "../../system/very/deep/../../../outside"
            app.aemu.importer.GuestLinks.validate(mapOf("apex/com.example/link" to target))
            assertFalse(GuestPathSafety.prospective(root, "apex/com.example/link", target, emptyMap(), emptySet()))
        } finally { root.deleteRecursively() }
    }

    @Test fun allowsExistingAbsoluteLinkOnlyInsideGuestRoot() {
        val root = Files.createTempDirectory("guest-link").toFile()
        try {
            root.resolve("system").mkdirs(); root.resolve("data/inside").mkdirs()
            try { Files.createSymbolicLink(root.resolve("data/ref").toPath(), root.resolve("data/inside").toPath()) }
            catch (e: java.nio.file.FileSystemException) { org.junit.Assume.assumeNoException(e) }
            assertTrue(GuestPathSafety.prospective(root,"system/link","../data/ref/file",emptyMap(),emptySet(),rejectMutable=false))
        } finally { root.deleteRecursively() }
    }

}
