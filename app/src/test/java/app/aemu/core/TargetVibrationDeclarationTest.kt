package app.aemu.core

import java.io.File
import java.io.IOException
import java.nio.file.Files
import java.nio.file.Path
import org.junit.Assert.*
import org.junit.Test

class TargetVibrationDeclarationTest {
    @Test fun diagnosticStageKeepsCauseWithoutIncludingItsPath() {
        for (stage in VibrationDeclarationStage.entries) {
            val cause = java.nio.file.AccessDeniedException("private-path")
            try {
                vibrationDeclarationStage<Unit>(stage) { throw cause }
                fail("expected stage failure")
            } catch (failure: VibrationDeclarationStageException) {
                assertEquals(stage, failure.stage)
                assertSame(cause, failure.cause)
                assertFalse(failure.message.orEmpty().contains("private-path"))
            }
        }
    }

    @Test fun unsafeRootReportsDirectoryStage() = withRoot { root ->
        val notDirectory = root.resolve("file").apply { writeText("file") }
        try { prepare(notDirectory, true); fail("expected unsafe root failure") }
        catch (failure: VibrationDeclarationStageException) {
            assertEquals(VibrationDeclarationStage.DIRECTORY, failure.stage)
            assertTrue(failure.cause is IOException)
        }
    }

    private fun prepare(root: File, enabled: Boolean, model: String = "NX809J",
        publish: (Path, Path) -> Unit = { stage, target -> Files.createLink(target, stage); Unit }): Boolean =
        TargetVibrationDeclaration.prepareWithPublisher(root, 36, model, "arm64-v8a", Engine.N64, enabled, publish)

    @Test fun publicationFailureKeepsForeignDataAndCleansTemp() = withRoot { root ->
        try {
            prepare(root, true, publish = { _, _ -> throw IOException("unavailable") })
            fail("expected publication failure")
        } catch (failure: VibrationDeclarationStageException) {
            assertEquals(VibrationDeclarationStage.PUBLISH, failure.stage)
        }
        val dir = declaration(root).parentFile!!
        assertTrue(dir.listFiles()!!.isEmpty())
    }

    @Test fun publicationCollisionNeverOverwritesForeignData() = withRoot { root ->
        try {
            prepare(root, true, publish = { _, target ->
                Files.write(target, "foreign".toByteArray())
                throw java.nio.file.FileAlreadyExistsException("target")
            })
            fail("expected conflict")
        } catch (failure: VibrationDeclarationStageException) {
            assertEquals(VibrationDeclarationStage.PUBLISH, failure.stage)
        }
        assertEquals("foreign", declaration(root).readText())
        assertEquals(1, declaration(root).parentFile!!.listFiles()!!.size)
    }

    private fun withRoot(test: (File) -> Unit) {
        val root = Files.createTempDirectory("vibration-declaration").toFile()
        try { test(root) } finally { root.deleteRecursively() }
    }

    private fun declaration(root: File) = root.resolve("vendor/etc/vintf/manifest/aemu-redmagic-vibrator.xml")

    @Test fun disabledAndNonTargetDoNotCreateDeclaration() = withRoot { root ->
        assertFalse(prepare(root, false))
        assertFalse(prepare(root, true, "other"))
        assertFalse(declaration(root).exists())
    }

    @Test fun targetDeclarationIsIdempotentAndCanBeDisabled() = withRoot { root ->
        assertTrue(prepare(root, true))
        val file = declaration(root)
        val bytes = file.readBytes()
        val text = bytes.toString(Charsets.UTF_8)
        assertTrue(text.contains("<name>android.hardware.vibrator</name>"))
        assertTrue(text.contains("<version>2</version>"))
        assertTrue(text.contains("<fqname>IVibrator/default</fqname>"))
        assertTrue(prepare(root, true))
        assertArrayEquals(bytes, file.readBytes())
        assertFalse(prepare(root, false))
        assertFalse(file.exists())
    }

    @Test fun foreignDeclarationIsNeverOverwrittenOrRemoved() = withRoot { root ->
        val file = declaration(root)
        assertTrue(file.parentFile!!.mkdirs())
        file.writeText("foreign")
        try { prepare(root, true); fail("expected conflict") } catch (_: IOException) { }
        assertEquals("foreign", file.readText())
        assertFalse(prepare(root, false))
        assertEquals("foreign", file.readText())
    }

    @Test fun disabledDoesNotRemoveForeignDirectory() = withRoot { root ->
        val file = declaration(root)
        assertTrue(file.mkdirs())
        assertFalse(prepare(root, false))
        assertTrue(file.isDirectory)
    }

    @Test fun disabledDoesNotFollowOrRemoveForeignSymlink() = withRoot { root ->
        val file = declaration(root)
        assertTrue(file.parentFile!!.mkdirs())
        val target = root.resolve("foreign.txt").apply { writeText("foreign") }
        try { Files.createSymbolicLink(file.toPath(), target.toPath()) }
        catch (_: UnsupportedOperationException) { org.junit.Assume.assumeTrue(false) }
        catch (_: java.nio.file.FileSystemException) { org.junit.Assume.assumeTrue(false) }
        assertFalse(prepare(root, false))
        assertTrue(Files.isSymbolicLink(file.toPath()))
        assertEquals("foreign", target.readText())
    }

    @Test fun doesNotDuplicateActiveOriginalDeclaration() = withRoot { root ->
        val file = root.resolve("vendor/etc/vintf/manifest/vendor.qti.hardware.vibrator.service.xml")
        assertTrue(file.parentFile!!.mkdirs())
        file.writeText("original")
        try { prepare(root, true); fail("expected active original conflict") } catch (_: IOException) { }
        assertEquals("original", file.readText())
        assertFalse(declaration(root).exists())
    }
}
