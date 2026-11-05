package app.aemu.importer
import org.junit.Assert.*
import org.junit.Test
import java.io.ByteArrayInputStream
import java.io.IOException
import java.nio.file.Files

class TargetTreeTest {
    private fun tree(vararg rows: Pair<String, ByteArray>) = object: TreeSource {
        override fun walk(visit: (String, Boolean, String?, Int, Long, (() -> java.io.InputStream)?) -> Unit) {
            for ((name, data) in rows) visit(name, false, null, 0x81a4, data.size.toLong()) { ByteArrayInputStream(data) }
        }
    }
    @Test fun movesSystemAsRootAndMountsRemainingPartitions() {
        val root = Files.createTempDirectory("target-tree").toFile()
        try {
            val target = TargetTree(root, { false })
            target.partition("system", tree("system/build.prop" to byteArrayOf(1), "system/bin/init" to byteArrayOf(2)))
            target.partition("vendor_dlkm", tree("lib/modules/a" to byteArrayOf(3)))
            target.finish(emptyList())
            assertArrayEquals(byteArrayOf(1), root.resolve("system/build.prop").readBytes())
            assertArrayEquals(byteArrayOf(3), root.resolve("vendor_dlkm/lib/modules/a").readBytes())
        } finally { root.deleteRecursively() }
    }
    @Test fun rejectsEscapingFile() {
        val root = Files.createTempDirectory("target-tree").toFile()
        try {
            val target = TargetTree(root, { false })
            try { target.partition("system", tree("../out" to byteArrayOf(1))); fail() } catch (_: IOException) { }
        } finally { root.deleteRecursively() }
    }
    @Test fun rejectsUnexpectedLinkDirectoryConflict() {
        val root = Files.createTempDirectory("target-tree").toFile()
        try {
            val target = TargetTree(root, { false })
            val source = object : TreeSource {
                override fun walk(visit: (String, Boolean, String?, Int, Long, (() -> java.io.InputStream)?) -> Unit) {
                    visit("a/child", false, null, 0x81a4, 1) { ByteArrayInputStream(byteArrayOf(1)) }
                    visit("a", false, "b", 0xa1ff, 0, null)
                }
            }
            target.partition("system", source)
            try { target.finish(emptyList()); fail() } catch (_: IOException) { }
        } finally { root.deleteRecursively() }
    }

    @Test fun laterPartitionMasksSystemMountPoint() {
        val root = Files.createTempDirectory("target-tree").toFile()
        try {
            val target = TargetTree(root, { false })
            val system = object: TreeSource {
                override fun walk(visit: (String, Boolean, String?, Int, Long, (() -> java.io.InputStream)?) -> Unit) {
                    visit("system/build.prop", false, null, 0x81a4, 1) { ByteArrayInputStream(byteArrayOf(1)) }
                    visit("odm/etc", false, "/vendor/odm/etc", 0xa1ff, 0, null)
                }
            }
            target.partition("system", system)
            target.partition("odm", tree("etc/config" to byteArrayOf(2)))
            target.finish(emptyList())
            assertArrayEquals(byteArrayOf(2), root.resolve("odm/etc/config").readBytes())
        } finally { root.deleteRecursively() }
    }

    @Test fun importedModeMakesRegularFilesReadableToGuest() {
        val root = Files.createTempDirectory("target-mode")
        try {
            org.junit.Assume.assumeTrue(Files.getFileStore(root).supportsFileAttributeView("posix"))
            val file = Files.write(root.resolve("guest"), byteArrayOf(1))
            Files.setPosixFilePermissions(file, java.nio.file.attribute.PosixFilePermissions.fromString("rw-------"))
            TargetTree.applyFileMode(file.toFile(), 0x81a4)
            val mode = Files.getPosixFilePermissions(file)
            assertTrue(mode.contains(java.nio.file.attribute.PosixFilePermission.GROUP_READ))
            assertTrue(mode.contains(java.nio.file.attribute.PosixFilePermission.OTHERS_READ))
        } finally { root.toFile().deleteRecursively() }
    }

    @Test fun legacyBinFilesBecomeExecutable() {
        val root = Files.createTempDirectory("target-bin").toFile()
        try {
            org.junit.Assume.assumeTrue(Files.getFileStore(root.toPath()).supportsFileAttributeView("posix"))
            val target = TargetTree(root, { false })
            target.partition("system", tree("bin/noexec" to byteArrayOf(1)))
            val file = root.resolve("system/bin/noexec").toPath()
            Files.setPosixFilePermissions(file, java.nio.file.attribute.PosixFilePermissions.fromString("rw-r--r--"))
            target.finish(emptyList())
            assertTrue(Files.getPosixFilePermissions(file).contains(java.nio.file.attribute.PosixFilePermission.OTHERS_EXECUTE))
        } finally { root.deleteRecursively() }
    }

    @Test fun addsLegacyMtabLinkWhenSystemEtcExists() {
        val root = Files.createTempDirectory("target-mtab").toFile()
        try {
            val probe = root.resolve("probe").toPath()
            try { Files.createSymbolicLink(probe, java.nio.file.Paths.get("missing")); Files.delete(probe) }
            catch (e: java.nio.file.FileSystemException) { org.junit.Assume.assumeNoException(e) }
            val target = TargetTree(root, { false })
            target.partition("system", tree("etc/config" to byteArrayOf(1)))
            target.finish(emptyList())
            val mtab = root.resolve("system/etc/mtab").toPath()
            assertTrue(Files.isSymbolicLink(mtab))
            assertEquals("../../proc/mounts", Files.readSymbolicLink(mtab).toString().replace('\\', '/'))
        } finally { root.deleteRecursively() }
    }

    @Test fun addsIoctlOnlyWhenToolboxContainsApplet() {
        val root = Files.createTempDirectory("target-toolbox").toFile()
        try {
            val probe = root.resolve("probe").toPath()
            try { Files.createSymbolicLink(probe, java.nio.file.Paths.get("missing")); Files.delete(probe) }
            catch (e: java.nio.file.FileSystemException) { org.junit.Assume.assumeNoException(e) }
            val target = TargetTree(root, { false })
            target.partition("system", tree("bin/toolbox" to "prefix\u0000ioctl\u0000suffix".toByteArray()))
            target.finish(emptyList())
            val link = root.resolve("system/bin/ioctl").toPath()
            assertTrue(Files.isSymbolicLink(link))
            assertEquals("toolbox", Files.readSymbolicLink(link).toString())
        } finally { root.deleteRecursively() }
    }

}
