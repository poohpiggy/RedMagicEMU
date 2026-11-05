package app.aemu.core
import org.junit.Assert.*
import org.junit.Test
import java.nio.file.Files
import java.nio.file.Paths

class PartitionLinkPlanTest {
    @Test fun preservesGuestTargetWhenPartitionMoves() {
        val root=Files.createTempDirectory("partition-plan").toFile()
        try {
            root.resolve("system/product/app").mkdirs(); root.resolve("system/product/lib64").mkdirs(); root.resolve("product").mkdir()
            try {
                Files.createSymbolicLink(root.resolve("system/product/app/absolute").toPath(),Paths.get("../../../product/lib64/x.so"))
                Files.createSymbolicLink(root.resolve("system/product/app/local").toPath(),Paths.get("target.so"))
            } catch(e: java.nio.file.FileSystemException) { org.junit.Assume.assumeNoException(e) }
            val rules=PartitionLinkPlan.plan(root,"product")
            assertEquals(1,rules.size)
            assertEquals("system/product/app/absolute",rules[0].oldPath)
            assertEquals("product/app/absolute",rules[0].newPath)
            assertEquals("../../../product/lib64/x.so",rules[0].originalTarget)
            assertEquals("../../product/lib64/x.so",rules[0].updatedTarget)
        } finally { root.deleteRecursively() }
    }
    @Test fun refusesAmbiguousIntermediateSymlink() {
        val root=Files.createTempDirectory("partition-plan").toFile()
        try {
            root.resolve("system/product/app").mkdirs(); root.resolve("product").mkdir()
            try {
                Files.createSymbolicLink(root.resolve("system/alias").toPath(),Paths.get("product"))
                Files.createSymbolicLink(root.resolve("system/product/app/link").toPath(),Paths.get("../../alias/../product/x"))
            } catch(e: java.nio.file.FileSystemException) { org.junit.Assume.assumeNoException(e) }
            assertThrows(java.io.IOException::class.java) { PartitionLinkPlan.plan(root,"product") }
        } finally { root.deleteRecursively() }
    }
    @Test fun refusesMissingIntermediateBeforeDotDot() {
        val root=Files.createTempDirectory("partition-plan").toFile()
        try {
            root.resolve("system/product/app").mkdirs(); root.resolve("product").mkdir()
            try { Files.createSymbolicLink(root.resolve("system/product/app/link").toPath(),Paths.get("../../missing/../product/x")) }
            catch(e: java.nio.file.FileSystemException) { org.junit.Assume.assumeNoException(e) }
            assertThrows(java.io.IOException::class.java) { PartitionLinkPlan.plan(root,"product") }
        } finally { root.deleteRecursively() }
    }

    @Test fun permitsDanglingTailWithoutCollapsingIt() {
        val root=Files.createTempDirectory("partition-plan").toFile()
        try {
            root.resolve("system/product/app").mkdirs(); root.resolve("product").mkdir()
            try { Files.createSymbolicLink(root.resolve("system/product/app/link").toPath(),Paths.get("../../../product/missing/file")) }
            catch(e: java.nio.file.FileSystemException) { org.junit.Assume.assumeNoException(e) }
            val plan = PartitionLinkPlan.plan(root,"product")
            assertEquals("../../product/missing/file",plan.single().updatedTarget)
        } finally { root.deleteRecursively() }
    }

}
