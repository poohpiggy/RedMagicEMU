package app.aemu.core

import android.content.ContextWrapper
import org.junit.Assert.*
import org.junit.Test
import java.io.File
import java.nio.file.Files

class InstanceDeletionTest {
    private class TestContext : ContextWrapper(null) {
        override fun getFilesDir(): File = root
        companion object { lateinit var root: File }
    }

    @Test fun refusesDeletionAndPreservesExistingInstance() {
        val root = Files.createTempDirectory("instance-delete").toFile()
        val dir = File(root, "images/abcdef").apply { mkdirs() }
        val data = File(dir, "data").apply { writeText("preserve") }
        try {
            TestContext.root = root
            val unsafeClass = Class.forName("sun.misc.Unsafe")
            val field = unsafeClass.getDeclaredField("theUnsafe").apply { isAccessible = true }
            val context = unsafeClass.getMethod("allocateInstance", Class::class.java).invoke(field.get(null), TestContext::class.java) as TestContext
            try {
                ImageStore.delete(context, "abcdef")
                fail("Deletion must be refused until it can be synchronized with the VM")
            } catch (_: IllegalStateException) { }
            assertEquals("preserve", data.readText())
        } finally { root.deleteRecursively() }
    }
}

