package app.aemu.core

import org.junit.Assert.*
import org.junit.Assume.assumeNoException
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File
import java.nio.file.Files
import java.nio.file.Paths

class TargetVariantIdBootTest {
    @get:Rule val temp = TemporaryFolder()
    private fun fixture(): File {
        val root = temp.root
        File(root, "vendor/bin/variantid").apply { parentFile!!.mkdirs(); writeText("fixture") }
        File(root, "dev/block/by-name/ztecfg").apply { parentFile!!.mkdirs(); writeBytes(ByteArray(524288)) }
        val alias = File(root, "dev/block/bootdevice/by-name/ztecfg").apply { parentFile!!.mkdirs() }
        try { Files.createSymbolicLink(alias.toPath(), Paths.get("../../by-name/ztecfg")) }
        catch (e: java.nio.file.FileSystemException) { assumeNoException("Symbolic links unavailable", e) }
        catch (e: UnsupportedOperationException) { assumeNoException("Symbolic links unavailable", e) }
        File(root, "vendor/etc/init/zcustom.rc").apply { parentFile!!.mkdirs(); writeText("""
            on load_persist_props_action
                start variantidBin
            service variantidBin /vendor/bin/variantid
                class main
                user root
                oneshot
        """.trimIndent()) }
        return root
    }
    @Test fun eligibleServiceRunsOnceBeforeFramework() {
        val root = fixture()
        assertTrue(TargetVariantId.eligible(root, 36, "NX809J", "arm64-v8a", true, true))
        val calls = mutableListOf<String>()
        val message = TargetVariantId.run { args -> calls.addAll(args); BoundedGuestCommand.Result.Completed(0, "secret") }
        assertEquals(listOf("/vendor/bin/variantid"), calls)
        assertFalse(message.contains("secret"))
        assertTrue(message.contains("exit 0"))
    }
    @Test fun requirementsAreChecked() {
        val root = fixture()
        assertFalse(TargetVariantId.eligible(root, 35, "NX809J", "arm64-v8a", true, true))
        assertFalse(TargetVariantId.eligible(root, 36, "other", "arm64-v8a", true, true))
        assertFalse(TargetVariantId.eligible(root, 36, "NX809J", "x86", true, true))
        assertFalse(TargetVariantId.eligible(root, 36, "NX809J", "arm64-v8a", false, true))
        assertFalse(TargetVariantId.eligible(root, 36, "NX809J", "arm64-v8a", true, false))
        File(root, "dev/block/by-name/ztecfg").delete()
        assertFalse(TargetVariantId.eligible(root, 36, "NX809J", "arm64-v8a", true, true))
    }
    @Test fun rejectsUnsafeAlias() {
        val root = fixture()
        val alias = File(root, "dev/block/bootdevice/by-name/ztecfg").toPath()
        Files.delete(alias)
        Files.createSymbolicLink(alias, File(root, "dev/block/by-name/ztecfg").toPath())
        assertFalse(TargetVariantId.eligible(root, 36, "NX809J", "arm64-v8a", true, true))
        Files.delete(alias)
        Files.createSymbolicLink(alias, Paths.get("../../by-name/other"))
        assertFalse(TargetVariantId.eligible(root, 36, "NX809J", "arm64-v8a", true, true))
    }
    @Test fun rejectsLinkedAliasParent() {
        val root = fixture()
        val aliasDir = File(root, "dev/block/bootdevice/by-name").toPath()
        Files.delete(aliasDir.resolve("ztecfg"))
        Files.delete(aliasDir)
        val external = File(root, "external").apply { mkdirs() }.toPath()
        Files.createSymbolicLink(external.resolve("ztecfg"), Paths.get("../dev/block/by-name/ztecfg"))
        Files.createSymbolicLink(aliasDir, external)
        assertFalse(TargetVariantId.eligible(root, 36, "NX809J", "arm64-v8a", true, true))
    }
    @Test fun wrongDeclarationCannotRun() {
        val root = fixture()
        File(root, "vendor/etc/init/zcustom.rc").writeText("service variantidBin /vendor/bin/other\n    oneshot\n")
        assertFalse(TargetVariantId.eligible(root, 36, "NX809J", "arm64-v8a", true, true))
    }
    @Test fun failureIsReportedWithoutOutput() {
        val failed = TargetVariantId.run { BoundedGuestCommand.Result.Completed(7, "private") }
        assertTrue(failed.contains("exit 7")); assertFalse(failed.contains("private"))
        try {
            TargetVariantId.run { BoundedGuestCommand.Result.Uncertain(BoundedGuestCommand.Reason.TIMEOUT, false, false, false, true) }
            fail("uncertain reader must prevent the framework from starting")
        } catch (error: IllegalStateException) {
            assertTrue(error.message!!.contains("TIMEOUT"))
        }
    }
}
