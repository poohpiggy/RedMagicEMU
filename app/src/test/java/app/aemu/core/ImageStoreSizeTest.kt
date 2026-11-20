package app.aemu.core

import org.junit.Assert.assertEquals
import org.junit.Assume.assumeNoException
import org.junit.Test
import java.nio.file.FileSystemException
import java.nio.file.Files

class ImageStoreSizeTest {
    @Test fun countsOnlyFileBytesAndTreatsMissingEntriesAsEmpty() {
        val root = Files.createTempDirectory("image-size")
        try {
            val system = Files.createDirectory(root.resolve("system"))
            Files.write(system.resolve("build.prop"), ByteArray(7))
            Files.write(root.resolve("props.base"), ByteArray(3))
            assertEquals(10L, ImageStore.du(root.toFile()))
            assertEquals(0L, ImageStore.du(root.resolve("missing").toFile()))
        } finally {
            root.toFile().deleteRecursively()
        }
    }

    @Test fun skipsFileAndDirectoryLinks() {
        val root = Files.createTempDirectory("image-size-links")
        try {
            val system = Files.createDirectory(root.resolve("system"))
            val prop = Files.write(system.resolve("build.prop"), ByteArray(7))
            try {
                Files.createSymbolicLink(root.resolve("prop-link"), prop)
                Files.createSymbolicLink(root.resolve("system-link"), system)
            } catch (e: FileSystemException) {
                assumeNoException(e)
            }
            assertEquals(7L, ImageStore.du(root.toFile()))
        } finally {
            Files.deleteIfExists(root.resolve("system-link"))
            Files.deleteIfExists(root.resolve("prop-link"))
            root.toFile().deleteRecursively()
        }
    }
}
