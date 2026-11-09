package app.aemu.core

import org.junit.Assert.*
import org.junit.Test
import java.io.File
import java.io.IOException
import java.nio.file.Files
import java.nio.file.StandardOpenOption
import java.util.concurrent.TimeUnit

class InstanceRuntimeLeaseTest {
    // Windows' local provider does not expose fileKey. This identity seam tests
    // OS locking only; real VM acquisition never uses this weaker identity.
    private fun acquire(dir: File, id: String) =
        InstanceRuntimeLease.acquireForTests(dir, id) { it.toRealPath().toString() }

    private fun instance(block: (File) -> Unit) {
        val base = Files.createTempDirectory("rme-runtime-lease").toFile()
        try {
            val images = File(base, "images").apply { mkdir() }
            val instance = File(images, "abc234").apply { mkdir() }
            block(instance)
        } finally { base.deleteRecursively() }
    }

    @Test fun sameInstanceIsExclusiveAndReleaseAllowsRetry() = instance { dir ->
        acquire(dir, "abc234").use {
            try { acquire(dir, "abc234"); fail("second lease granted") }
            catch (_: IOException) { }
        }
        acquire(dir, "abc234").use { assertTrue(it.isValid()) }
    }

    @Test fun incorrectOrUnsafeInstanceNeverCreatesLock() = instance { dir ->
        try { acquire(dir, "def234"); fail("incorrect ID accepted") }
        catch (_: IOException) { }
        assertFalse(File(dir, InstanceRuntimeLease.FILE_NAME).exists())
        val outside = File(dir.parentFile, "outside").apply { mkdir() }
        val link = File(dir.parentFile, "def234").toPath()
        try { Files.createSymbolicLink(link, outside.toPath()) }
        catch (e: Exception) { org.junit.Assume.assumeNoException(e) }
        try { acquire(link.toFile(), "def234"); fail("symlink instance accepted") }
        catch (_: IOException) { }
        assertEquals(0, outside.listFiles()!!.size)
    }

    @Test fun existingLockSymlinkIsNotFollowed() = instance { dir ->
        val outside = File(dir.parentFile, "outside.txt").apply { writeText("preserve") }
        val link = File(dir, InstanceRuntimeLease.FILE_NAME).toPath()
        try { Files.createSymbolicLink(link, outside.toPath()) }
        catch (e: Exception) { org.junit.Assume.assumeNoException(e) }
        try { acquire(dir, "abc234"); fail("lock symlink accepted") }
        catch (_: IOException) { }
        assertEquals("preserve", outside.readText())
    }

    @Test fun anotherProcessBlocksAndDeathReleasesLock() = instance { dir ->
        val classes = File(InstanceRuntimeLeaseChild::class.java.protectionDomain.codeSource.location.toURI())
        val java = File(System.getProperty("java.home"), "bin/java").absolutePath
        val child = ProcessBuilder(java, "-cp", classes.absolutePath, InstanceRuntimeLeaseChild::class.java.name,
            File(dir, InstanceRuntimeLease.FILE_NAME).absolutePath).redirectErrorStream(true).start()
        try {
            assertEquals("LOCKED", child.inputStream.bufferedReader().readLine())
            try { acquire(dir, "abc234"); fail("cross-process lock ignored") }
            catch (_: IOException) { }
            child.destroyForcibly()
            assertTrue("child did not terminate", child.waitFor(10, TimeUnit.SECONDS))
            acquire(dir, "abc234").use { assertTrue(it.isValid()) }
        } finally { child.destroyForcibly(); child.waitFor(10, TimeUnit.SECONDS) }
    }

    @Test fun preexistingLockFileContentIsNotAltered() = instance { dir ->
        val lockFile = File(dir, InstanceRuntimeLease.FILE_NAME)
        Files.write(lockFile.toPath(), "preserve".toByteArray(), StandardOpenOption.CREATE_NEW)
        acquire(dir, "abc234").use { }
        assertEquals("preserve", lockFile.readText())
    }

    @Test fun unavailableFileIdentityFailsClosed() = instance { dir ->
        try { InstanceRuntimeLease.acquireForTests(dir, "abc234") { null }; fail("unverified identity accepted") }
        catch (_: IOException) { }
    }

    @Test fun descriptorAndPathIdentityMismatchFailsClosed() = instance { dir ->
        try {
            InstanceRuntimeLease.acquireForTests(dir, "abc234", { "opened inode" }, { "different inode" })
            fail("mismatched inode accepted")
        } catch (_: IOException) { }
        acquire(dir, "abc234").use { assertTrue(it.isValid()) }
    }

    @Test fun pathReplacementInvalidatesExistingLease() = instance { dir ->
        var pathKey = "inode-a"
        InstanceRuntimeLease.acquireForTests(dir, "abc234", { "inode-a" }, { pathKey }).use { lease ->
            assertTrue(lease.isValid())
            pathKey = "inode-b"
            assertFalse(lease.isValid())
        }
    }
}
