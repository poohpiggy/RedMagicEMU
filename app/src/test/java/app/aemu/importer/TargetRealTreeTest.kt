package app.aemu.importer

import java.io.File
import org.junit.Assume.assumeTrue
import org.junit.Assert.*
import org.junit.Test

/** Optional, local-only integration run. No firmware is included in the repository. */
class TargetRealTreeTest {
    @Test fun buildsDesignatedTreeWhenFixtureIsProvided() {
        val fixture = System.getProperty("redmagic.target.fixture")
        assumeTrue("No local target fixture", !fixture.isNullOrBlank())
        val dir = File(fixture!!)
        val root = File(dir, "guest-root")
        check(root.mkdir())
        try {
            val ramdisk = TargetPackageTree.extract(File(dir, "super.img"), File(dir, "ramdisk.img"), root) { false }
            assertTrue(ramdisk.isNotEmpty())
            assertTrue(File(root, "system").isDirectory)
            assertTrue("system build.prop missing", File(root, "system/build.prop").isFile)

        } finally {
            check(root.deleteRecursively())
        }
    }
}

