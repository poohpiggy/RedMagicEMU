package app.aemu.core

import android.content.ContextWrapper
import org.junit.Assert.*
import org.junit.Test
import java.io.File
import java.nio.file.Files

class InstanceResetTest {
    private class TestContext : ContextWrapper(null) {
        override fun getFilesDir(): File = root
        companion object { lateinit var root: File }
    }

    @Test fun refusesResetAndPreservesExistingInstance() {
        val root = Files.createTempDirectory("instance-reset").toFile()
        val dir = File(root, "images/abcdef").apply { mkdirs() }
        File(dir, "image.json").writeText("""{"id":"abcdef"}""")
        val data = File(dir, "root/data/user/important").apply { parentFile.mkdirs(); writeText("preserve") }
        try {
            TestContext.root = root
            val unsafeClass = Class.forName("sun.misc.Unsafe")
            val field = unsafeClass.getDeclaredField("theUnsafe").apply { isAccessible = true }
            val context = unsafeClass.getMethod("allocateInstance", Class::class.java).invoke(field.get(null), TestContext::class.java) as TestContext
            try {
                ImageStore.resetData(context, "abcdef")
                fail("Reset must be refused until it can be synchronized with the VM")
            } catch (e: IllegalStateException) {
                assertEquals("Instance reset is unavailable while VM coordination is incomplete", e.message)
            }
            assertEquals("preserve", data.readText())
        } finally { root.deleteRecursively() }
    }
}
