package app.aemu.importer

import org.junit.Assert.*
import org.junit.Test
import org.junit.Assume.assumeNoException
import java.io.File
import java.io.RandomAccessFile
import java.nio.file.Files

class TargetZtecfgTest {
    private fun requireSymlinks(work: File) {
        val probe = File(work, "link-probe").toPath()
        try { Files.createSymbolicLink(probe, java.nio.file.Paths.get("missing")) }
        catch (e: java.nio.file.FileSystemException) { assumeNoException("Symbolic links unavailable", e) }
        catch (e: UnsupportedOperationException) { assumeNoException("Symbolic links unavailable", e) }
        Files.delete(probe)
    }
    @Test fun exposesOnePrivateCopyAtBothPaths() {
        val work = Files.createTempDirectory("ztecfg-test").toFile()
        try {
            requireSymlinks(work)
            val root = File(work, "root").apply { mkdir() }
            val source = File(work, "source").apply { writeBytes(ByteArray(524288) { (it % 251).toByte() }) }
            TargetZtecfg.install(root, source)
            val first = File(root, "dev/block/by-name/ztecfg")
            val second = File(root, "dev/block/bootdevice/by-name/ztecfg")
            assertTrue("bootdevice alias must be a relative symbolic link", Files.isSymbolicLink(second.toPath()))
            assertEquals("../../by-name/ztecfg", Files.readSymbolicLink(second.toPath()).toString().replace('\\', '/'))
            RandomAccessFile(second, "r").use { f -> f.seek(4096); assertEquals(4096 % 251, f.read()) }
            RandomAccessFile(first, "rw").use { f -> f.seek(4096); f.write(123) }
            RandomAccessFile(second, "r").use { f -> f.seek(4096); assertEquals(123, f.read()) }
            RandomAccessFile(source, "r").use { f -> f.seek(4096); assertEquals(4096 % 251, f.read()) }
        } finally { work.deleteRecursively() }
    }
    @Test fun rejectsWrongSizeAndExistingTarget() {
        val work = Files.createTempDirectory("ztecfg-invalid").toFile()
        try {
            val root = File(work, "root").apply { mkdir() }
            val source = File(work, "source").apply { writeBytes(ByteArray(524287)) }
            try { TargetZtecfg.install(root, source); fail("short image accepted") }
            catch (_: java.io.IOException) { }
            source.writeBytes(ByteArray(524288))
            val target = File(root, "dev/block/by-name/ztecfg").apply { parentFile!!.mkdirs(); writeText("keep") }
            try { TargetZtecfg.install(root, source); fail("existing target overwritten") }
            catch (_: java.io.IOException) { }
            assertEquals("keep", target.readText())
        } finally { work.deleteRecursively() }
    }

    @Test fun rejectsNonDirectoryOnMappingPath() {
        val work = Files.createTempDirectory("ztecfg-path").toFile()
        try {
            val root = File(work, "root").apply { mkdir() }
            val source = File(work, "source").apply { writeBytes(ByteArray(524288)) }
            val block = File(root, "dev/block").apply { parentFile!!.mkdirs(); writeText("keep") }
            try { TargetZtecfg.install(root, source); fail("invalid directory accepted") }
            catch (_: java.io.IOException) { }
            assertEquals("keep", block.readText())
        } finally { work.deleteRecursively() }
    }

    @Test fun rejectsSymbolicLinkOnMappingPath() {
        val work = Files.createTempDirectory("ztecfg-link").toFile()
        try {
            val root = File(work, "root").apply { mkdir() }
            val external = File(work, "external").apply { mkdir() }
            val source = File(work, "source").apply { writeBytes(ByteArray(524288)) }
            File(root, "dev").mkdir()
            try { Files.createSymbolicLink(File(root, "dev/block").toPath(), external.toPath()) }
            catch (e: Exception) { assumeNoException("Symbolic links unavailable", e) }
            try { TargetZtecfg.install(root, source); fail("symbolic link accepted") }
            catch (_: java.io.IOException) { }
            assertTrue(external.listFiles()!!.isEmpty())
        } finally { work.deleteRecursively() }
    }

    @Test fun separateInstancesHaveIndependentCopies() {
        val work = Files.createTempDirectory("ztecfg-isolation").toFile()
        try {
            requireSymlinks(work)
            val first = File(work, "first").apply { mkdir() }
            val second = File(work, "second").apply { mkdir() }
            val source = File(work, "source").apply { writeBytes(ByteArray(524288)) }
            TargetZtecfg.install(first, source); TargetZtecfg.install(second, source)
            RandomAccessFile(File(first, "dev/block/by-name/ztecfg"), "rw").use { it.write(42) }
            assertEquals(0, File(second, "dev/block/by-name/ztecfg").inputStream().use { it.read() })
        } finally { work.deleteRecursively() }
    }

}
