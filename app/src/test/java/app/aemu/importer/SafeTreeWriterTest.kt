package app.aemu.importer

import org.junit.Assert.*
import org.junit.Test
import java.io.ByteArrayInputStream
import java.io.IOException
import java.nio.file.Files

class SafeTreeWriterTest {
    @Test fun writesConfinedFile() {
        val dir = Files.createTempDirectory("safe-tree").toFile()
        try {
            val w = SafeTreeWriter(dir, 10, 32, { false })
            w.write("system/bin/a", 3, ByteArrayInputStream(byteArrayOf(1, 2, 3)))
            assertArrayEquals(byteArrayOf(1, 2, 3), dir.resolve("system/bin/a").readBytes())
        } finally { dir.deleteRecursively() }
    }
    @Test fun rejectsTraversalAndPrefixConfusion() {
        val dir = Files.createTempDirectory("safe-tree").toFile()
        try {
            val w = SafeTreeWriter(dir, 10, 32, { false })
            for (name in listOf("../escape", "/absolute", "a/../../escape", "a\\escape", "a//b")) {
                try { w.write(name, 1, ByteArrayInputStream(byteArrayOf(1))); fail("accepted $name") } catch (_: IOException) { }
            }
        } finally { dir.deleteRecursively() }
    }
    @Test fun rejectsOversizeAndShortContent() {
        val dir = Files.createTempDirectory("safe-tree").toFile()
        try {
            val w = SafeTreeWriter(dir, 10, 4, { false })
            try { w.write("large", 5, ByteArrayInputStream(ByteArray(5))); fail() } catch (_: IOException) { }
            try { w.write("short", 3, ByteArrayInputStream(ByteArray(2))); fail() } catch (_: IOException) { }
            assertFalse(dir.resolve("short").exists())
        } finally { dir.deleteRecursively() }
    }
    @Test fun cancellationRemovesPartialFile() {
        val dir = Files.createTempDirectory("safe-tree").toFile()
        try {
            var checks = 0
            val w = SafeTreeWriter(dir, 10, 2_000_000, { ++checks > 2 })
            try { w.write("data", 1_500_000, ByteArrayInputStream(ByteArray(1_500_000))); fail() } catch (_: IOException) { }
            assertFalse(dir.resolve("data").exists())
        } finally { dir.deleteRecursively() }
    }
}
