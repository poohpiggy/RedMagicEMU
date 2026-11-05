package app.aemu.importer
import org.junit.Assert.*
import org.junit.Test
import java.nio.file.Files
import java.io.IOException
class SafeLinksTest {
    @Test fun rejectsEscapeBeforeCreatingAnyLink() {
        val dir = Files.createTempDirectory("safe-links").toFile()
        try {
            val w = SafeTreeWriter(dir, 10, 100, { false })
            try { w.links(mapOf("a" to "b", "b" to "../outside")); fail() } catch (_: IOException) { }
            assertEquals(0, dir.listFiles()!!.size)
        } finally { dir.deleteRecursively() }
    }
}
