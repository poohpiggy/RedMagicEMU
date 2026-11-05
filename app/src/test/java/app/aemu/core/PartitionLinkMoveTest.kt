package app.aemu.core
import org.junit.Assert.*
import org.junit.Test
import java.io.IOException
import java.nio.file.Files
import java.nio.file.Paths

class PartitionLinkMoveTest {
    private fun fixture(block: (java.io.File) -> Unit) {
        val root=Files.createTempDirectory("partition-move").toFile()
        try {
            root.resolve("system/product/app").mkdirs();root.resolve("system/product/lib64").mkdirs();root.resolve("product").mkdir()
            root.resolve("system/product/lib64/a.so").writeText("firmware")
            try { Files.createSymbolicLink(root.resolve("system/product/app/link").toPath(),Paths.get("../../../product/lib64/a.so")) }
            catch (e: java.nio.file.FileSystemException) { org.junit.Assume.assumeNoException(e) }
            block(root)
        } finally { root.deleteRecursively() }
    }
    @Test fun movesAndPreservesGuestTarget() = fixture { root ->
        assertTrue(PartitionLinkMove.move(root,"product"))
        val link=root.resolve("product/app/link").toPath()
        assertEquals("../../product/lib64/a.so",Files.readSymbolicLink(link).toString())
        assertEquals("firmware",link.toFile().readText())
        assertEquals("../product",Files.readSymbolicLink(root.resolve("system/product").toPath()).toString())
    }
    @Test fun failureRestoresOriginalTreeAndLink() = fixture { root ->
        assertThrows(IOException::class.java) { PartitionLinkMove.move(root,"product", beforeMove = { throw IOException("injected") }) }
        assertEquals("../../../product/lib64/a.so",Files.readSymbolicLink(root.resolve("system/product/app/link").toPath()).toString())
        assertTrue(root.resolve("product").isDirectory)
        assertFalse(Files.isSymbolicLink(root.resolve("system/product").toPath()))
        assertTrue(root.listFiles()!!.none { it.name.startsWith(".rootlinks-") })
    }
    @Test fun failureAfterDirectoryMoveRestoresOriginalTree() = fixture { root ->
        assertThrows(IOException::class.java) { PartitionLinkMove.move(root,"product", afterMove = { throw IOException("injected") }) }
        assertEquals("../../../product/lib64/a.so",Files.readSymbolicLink(root.resolve("system/product/app/link").toPath()).toString())
        assertTrue(root.resolve("product").isDirectory)
        assertFalse(Files.isSymbolicLink(root.resolve("system/product").toPath()))
        assertTrue(root.listFiles()!!.none { it.name.startsWith(".rootlinks-") })
    }

    @Test fun recoversInterruptedRebaseUsingAuthenticatedRules() = fixture { root ->
        val partition=PartitionCompatibility.plan(root).single { it.name=="product" }
        val links=PartitionLinkPlan.plan(root,"product")
        val txn=root.resolve(".rootlinks-product-txn").apply { mkdir() }
        Files.move(root.resolve("product").toPath(), txn.resolve("placeholder").toPath())
        val link=root.resolve("system/product/app/link").toPath()
        Files.delete(link); Files.createSymbolicLink(link,Paths.get(links.single().updatedTarget))
        PartitionLinkMove.recover(root,partition,links)
        assertEquals(links.single().originalTarget, Files.readSymbolicLink(link).toString())
        assertTrue(root.resolve("product").isDirectory)
        assertFalse(txn.exists())
    }
    @Test fun refusesUnknownTransactionContentWithoutDeletingIt() = fixture { root ->
        val partition=PartitionCompatibility.plan(root).single { it.name=="product" }
        val links=PartitionLinkPlan.plan(root,"product")
        val txn=root.resolve(".rootlinks-product-txn").apply { mkdir() }
        val unknown=txn.resolve("personal").apply { writeText("keep") }
        assertThrows(IOException::class.java) { PartitionLinkMove.recover(root,partition,links) }
        assertEquals("keep",unknown.readText())
    }

    @Test fun recoversAfterDirectoryMoveAndAfterSourceLinkCreation() {
        for (sourceLink in listOf(false,true)) fixture { root ->
            val partition=PartitionCompatibility.plan(root).single { it.name=="product" }
            val links=PartitionLinkPlan.plan(root,"product")
            val txn=root.resolve(".rootlinks-product-txn").apply { mkdir() }
            Files.move(root.resolve("product").toPath(),txn.resolve("placeholder").toPath())
            val old=root.resolve("system/product/app/link").toPath(); Files.delete(old); Files.createSymbolicLink(old,Paths.get(links.single().updatedTarget))
            Files.move(root.resolve("system/product").toPath(),root.resolve("product").toPath())
            if (sourceLink) Files.createSymbolicLink(root.resolve("system/product").toPath(),Paths.get("../product"))
            PartitionLinkMove.recover(root,partition,links)
            assertEquals(links.single().originalTarget,Files.readSymbolicLink(root.resolve("system/product/app/link").toPath()).toString())
            assertTrue(root.resolve("product").isDirectory); assertFalse(txn.exists())
        }
    }
    @Test fun finalizesCompletedMoveIfBackupAlreadyRemoved() = fixture { root ->
        val partition=PartitionCompatibility.plan(root).single { it.name=="product" }
        val links=PartitionLinkPlan.plan(root,"product")
        assertTrue(PartitionLinkMove.move(root,"product"))
        val txn=root.resolve(".rootlinks-product-txn").apply { mkdir() }
        PartitionLinkMove.recover(root,partition,links)
        assertFalse(txn.exists()); assertTrue(Files.isSymbolicLink(root.resolve("system/product").toPath()))
        assertEquals("firmware",root.resolve("product/app/link").readText())
    }
    @Test fun refusesUnexpectedLinkTargetWithoutChangingTree() = fixture { root ->
        val partition=PartitionCompatibility.plan(root).single { it.name=="product" }
        val links=PartitionLinkPlan.plan(root,"product")
        val txn=root.resolve(".rootlinks-product-txn").apply { mkdir() }
        Files.move(root.resolve("product").toPath(),txn.resolve("placeholder").toPath())
        val path=root.resolve("system/product/app/link").toPath(); Files.delete(path); Files.createSymbolicLink(path,Paths.get("unexpected"))
        assertThrows(IOException::class.java) { PartitionLinkMove.recover(root,partition,links) }
        assertEquals("unexpected",Files.readSymbolicLink(path).toString()); assertTrue(txn.exists())
    }

}
