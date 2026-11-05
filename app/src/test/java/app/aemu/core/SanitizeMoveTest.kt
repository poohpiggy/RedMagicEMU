package app.aemu.core
import org.junit.Assert.*
import org.junit.Test
import java.nio.file.Files

class SanitizeMoveTest {
    @Test fun movesRatherThanLosingOriginal() {
        val root = Files.createTempDirectory("sanitize").toFile()
        try {
            val source = root.resolve("vendor/lib/hw/x.so"); source.parentFile.mkdirs(); source.writeText("original")
            val parked = root.resolve("system/.aemu-parked"); parked.mkdirs()
            assertTrue(SanitizeMove.park(root, parked, source))
            assertFalse(source.exists())
            assertEquals("original", parked.resolve("vendor#lib#hw#x.so").readText())
        } finally { root.deleteRecursively() }
    }
    @Test fun refusesNonemptyDestinationInsteadOfIgnoringFailure() {
        val root = Files.createTempDirectory("sanitize").toFile()
        try {
            val source = root.resolve("a").apply { writeText("original") }
            val parked = root.resolve("parked").apply { mkdir() }
            parked.resolve("a").apply { mkdir(); resolve("child").writeText("preserve") }
            try { SanitizeMove.park(root, parked, source); fail() } catch (_: java.io.IOException) { }
            assertEquals("original", source.readText())
        } finally { root.deleteRecursively() }
    }
}
