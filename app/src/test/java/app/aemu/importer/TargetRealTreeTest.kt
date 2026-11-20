package app.aemu.importer

import java.io.File
import java.nio.file.Files
import org.junit.Assume.assumeTrue
import org.junit.Assert.*
import org.junit.Test

/** Optional, local-only integration run. No firmware is included in the repository. */
class TargetRealTreeTest {
    @Test fun buildsDesignatedTreeWhenFixtureIsProvided() {
        val fixture = System.getProperty("redmagic.target.fixture")
        assumeTrue("No local target fixture", !fixture.isNullOrBlank())
        val dir = File(fixture!!)
        val instance = Files.createTempDirectory("target-real-tree").toFile()
        val root = File(instance, "root")
        check(root.mkdir())
        try {
            val ramdisk = TargetPackageTree.extract(File(dir, "super.img"), File(dir, "ramdisk.img"), root) { false }
            assertTrue(ramdisk.isNotEmpty())
            assertTrue(File(root, "system").isDirectory)
            assertTrue("system build.prop missing", File(root, "system/build.prop").isFile)
            assertFalse(File(instance, "partition-images").exists())

        } finally {
            check(instance.deleteRecursively())
        }
    }
}

