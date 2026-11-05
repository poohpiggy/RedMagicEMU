package app.aemu.core
import org.junit.Assert.*
import org.junit.Test
import java.nio.file.Files
import java.nio.file.Paths

class ApexLinkTest {
    @Test fun ordinarySystemApexUsesCorrectRelativePath() {
        val root=Files.createTempDirectory("apex-link").toFile()
        try {
            val src=root.resolve("system/apex").apply { mkdirs() }
            root.resolve("apex").mkdir()
            assertEquals("../../apex/com.example", ApexLink.relativeTarget(root,src,"com.example"))
        } finally { root.deleteRecursively() }
    }
    @Test fun migratedSystemExtDoesNotEscapeGuestRoot() {
        val root=Files.createTempDirectory("apex-link").toFile()
        try {
            root.resolve("system").mkdir();root.resolve("apex").mkdir();root.resolve("system_ext/apex").mkdirs()
            try { Files.createSymbolicLink(root.resolve("system/system_ext").toPath(), Paths.get("../system_ext")) }
            catch (e: java.nio.file.FileSystemException) { org.junit.Assume.assumeNoException(e) }
            assertEquals("../../apex/com.example", ApexLink.relativeTarget(root,root.resolve("system/system_ext/apex"),"com.example"))
        } finally { root.deleteRecursively() }
    }
}
