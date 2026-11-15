package app.aemu.importer

import app.aemu.core.InstanceAdmission
import app.aemu.core.PartitionLinkMove
import app.aemu.core.Engine
import app.aemu.core.GuestImage
import app.aemu.core.VmSettings
import java.io.File
import java.io.IOException
import java.io.RandomAccessFile
import java.nio.file.Files
import java.nio.file.LinkOption.NOFOLLOW_LINKS
import java.nio.file.attribute.BasicFileAttributes
import java.security.MessageDigest
import org.junit.Assert.*
import org.junit.Assume.assumeNoException
import org.junit.Assume.assumeTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class TargetAodImportStageTest {
    @get:Rule val temp = TemporaryFolder()
    private val id = "abcdef"
    private val apkBytes = "synthetic SystemUI".toByteArray()
    private val proxyBytes = "synthetic proxy".toByteArray()
    private val maskBytes = "synthetic alpha".toByteArray()
    private val modelBytes = "synthetic model".toByteArray()
    private fun digest(bytes: ByteArray) = MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") { "%02x".format(it) }
    private fun inputs() = TargetAodImportStage.Inputs(proxyBytes, digest(proxyBytes), maskBytes)
    private fun expected() = TargetAodImportStage.TestIdentity(digest(apkBytes), digest(maskBytes), maskBytes.size, digest(modelBytes))
    private fun root(lease: PendingImports.Lease) = File(lease.dir, "$id/root")
    private fun apk(root: File) = File(root, "system/system_ext/priv-app/SystemUI_MFV/SystemUI_MFV.apk")
    private fun library(root: File) = File(root, "system/system_ext/priv-app/SystemUI_MFV/lib")

    private fun fixture(): Pair<PendingImports.Lease, File> {
        val images = temp.newFolder()
        assumeTrue("filesystem does not provide stable directory identities",
            Files.readAttributes(images.toPath(), BasicFileAttributes::class.java, NOFOLLOW_LINKS).fileKey() != null)
        val lease = PendingImports.begin(images)
        val root = root(lease)
        check(root.mkdirs())
        File(root, "system/build.prop").apply { parentFile!!.mkdirs(); writeText("ro.build.fingerprint=${InstanceAdmission.SYSTEM_FINGERPRINT}\n") }
        apk(root).apply { parentFile!!.mkdirs(); writeBytes(apkBytes) }
        File(root, "system/etc/custom_config/app/systemUI/depth/wallpaperMattingCache.bin").apply {
            parentFile!!.mkdirs(); writeBytes(modelBytes)
        }
        File(root, "system/carrier/projects/IP_MC_W_CN/prop_fixed.txt").apply {
            parentFile!!.mkdirs()
            writeText("ro.vendor.build.def_theme_name=default_theme_64\npersist.vendor.custom.variant.id=IP_MC_W_CN\n")
        }
        val cfg = File(root, "dev/block/by-name/ztecfg").apply { parentFile!!.mkdirs(); writeBytes(ByteArray(524288)) }
        RandomAccessFile(cfg, "rw").use { io ->
            io.seek(4096)
            listOf("this-is-cfg-head", "zte_variantflag", "cfg-signature-is-here", "IP_MC_W_CN").forEach {
                val bytes = it.toByteArray(Charsets.US_ASCII)
                io.write(bytes); io.write(ByteArray(32 - bytes.size))
            }
        }
        return lease to root
    }

    private fun stage(lease: PendingImports.Lease) =
        TargetAodImportStage.stageFixture(lease, id, inputs(), expected())

    private val target = TargetAodImportStage.Target(36, "NX809J", "arm64-v8a", Engine.N64)
    private fun image() = GuestImage(
        id = id, name = "fixture", release = "16", api = 36, brand = "REDMAGIC", model = "NX809J",
        skin = "RedMagic", engine = Engine.N64, abi = "arm64-v8a", bootclasspath = "",
        exports = emptyMap(), dirs = emptyList(), services = emptyList(), sdcardPath = "/sdcard",
        settings = VmSettings(), createdAt = 0,
    )

    private fun prepare(lease: PendingImports.Lease, target: TargetAodImportStage.Target = this.target,
                        cancelled: () -> Boolean = { false },
                        afterProxy: (File) -> Unit = {},
                        source: () -> TargetAodImportStage.Inputs = { inputs() }) =
        TargetAodImportStage.prepareFixture(lease, id, target, expected(), cancelled, afterProxy, source)

    @Test fun newPrivateInstanceStagesOnlyVerifiedFilesAndSurvivesPartitionMove() {
        val (lease, root) = fixture()
        lease.use {
            stage(lease)
            val lib = library(root)
            assertArrayEquals(proxyBytes, File(lib, "arm64/libzte_saliency.so").readBytes())
            assertArrayEquals(maskBytes, File(lib, "arm64/rme_aod_mask.raw").readBytes())
            assertArrayEquals(apkBytes, apk(root).readBytes())
            assertTrue(PartitionLinkMove.move(root, "system_ext"))
            assertArrayEquals(proxyBytes, File(root, "system_ext/priv-app/SystemUI_MFV/lib/arm64/libzte_saliency.so").readBytes())
            assertArrayEquals(maskBytes, File(root, "system_ext/priv-app/SystemUI_MFV/lib/arm64/rme_aod_mask.raw").readBytes())
        }
    }

    @Test fun rejectsWrongFirmwareVariantApkAndArtifactsBeforeCreatingAnything() {
        for (kind in listOf("fingerprint", "variant", "apk", "model", "proxy", "mask")) {
            val (lease, root) = fixture()
            lease.use {
                when (kind) {
                    "fingerprint" -> File(root, "system/build.prop").writeText("ro.build.fingerprint=wrong\n")
                    "variant" -> RandomAccessFile(File(root, "dev/block/by-name/ztecfg"), "rw").use { io -> io.seek(4096 + 96L); io.write("GEN_CN\u0000".toByteArray()) }
                    "apk" -> apk(root).writeText("wrong")
                    "model" -> File(root, "system/etc/custom_config/app/systemUI/depth/wallpaperMattingCache.bin").writeText("wrong")
                }
                val input = when (kind) {
                    "proxy" -> inputs().copy(proxySha256 = "00".repeat(32))
                    "mask" -> inputs().copy(mask = byteArrayOf(1))
                    else -> inputs()
                }
                assertThrows("$kind must be rejected", IOException::class.java) {
                    TargetAodImportStage.stageFixture(lease, id, input, expected())
                }
                assertFalse("$kind created library directory", library(root).exists())
            }
        }
    }

    @Test fun refusesExistingDirectoryWithoutChangingItsContents() {
        val (lease, root) = fixture()
        lease.use {
            val existing = File(library(root), "arm64/libzte_saliency.so").apply { parentFile!!.mkdirs(); writeText("existing") }
            assertThrows(IOException::class.java) { stage(lease) }
            assertEquals("existing", existing.readText())
        }
    }

    @Test fun cancellationAfterFirstFileRemovesOnlyThisPrivateAttempt() {
        val (lease, root) = fixture()
        lease.use {
            var checks = 0
            assertThrows(ImportCancelledException::class.java) {
                TargetAodImportStage.stageFixture(lease, id, inputs(), expected()) { ++checks >= 4 }
            }
            assertFalse(library(root).exists())
            assertArrayEquals(apkBytes, apk(root).readBytes())
        }
    }

    @Test fun refusesLinkedParentWithoutWritingOutsideTheImport() {
        val (lease, root) = fixture()
        lease.use {
            val outside = temp.newFolder()
            val lib = library(root)
            try { Files.createSymbolicLink(lib.toPath(), outside.toPath()) }
            catch (e: Exception) { assumeNoException(e) }
            assertThrows(IOException::class.java) { stage(lease) }
            assertEquals(emptyList<String>(), outside.list()!!.toList())
        }
    }

    @Test fun closedOrPublishedLeaseCannotModifyTheInstance() {
        val (lease, root) = fixture()
        File(root.parentFile, "image.json").writeText("complete")
        val images = lease.dir.parentFile!!.parentFile!!
        val published = lease.commit(id)
        assertTrue(published.exists())
        assertThrows(IOException::class.java) { stage(lease) }
        assertFalse(File(images, "$id/root/system/system_ext/priv-app/SystemUI_MFV/lib").exists())
        lease.close()
        assertThrows(IOException::class.java) { stage(lease) }
    }

    @Test fun otherVariantsAndMissingZtecfgDoNotReadAssetsOrChangeImport() {
        for (kind in listOf("other-target", "missing-cfg", "generic", "wrong-fingerprint")) {
            val (lease, root) = fixture()
            lease.use {
                when (kind) {
                    "missing-cfg" -> assertTrue(File(root, "dev/block/by-name/ztecfg").delete())
                    "generic" -> {
                        RandomAccessFile(File(root, "dev/block/by-name/ztecfg"), "rw").use { io -> io.seek(4096 + 96L); io.write("GEN_CN\u0000".toByteArray()) }
                        File(root, "system/carrier/projects/GEN_CN/prop_fixed.txt").apply { parentFile!!.mkdirs(); writeText("ro.vendor.build.def_theme_name=generic\n") }
                    }
                    "wrong-fingerprint" -> File(root, "system/build.prop").writeText("ro.build.fingerprint=other\n")
                }
                var read = false
                val selected = if (kind == "other-target") target.copy(api = 35) else target
                val result = prepare(lease, selected, source = { read = true; inputs() })
                assertTrue("$kind: $result", result is TargetAodImportStage.Result.NotApplicable)
                assertFalse("$kind read assets", read)
                assertFalse("$kind wrote lib", library(root).exists())
            }
        }
    }

    @Test fun matchingTargetReportsPreparedOnlyAfterWritingBothFiles() {
        val (lease, root) = fixture()
        lease.use {
            assertEquals(TargetAodImportStage.Result.Prepared, prepare(lease))
            assertArrayEquals(proxyBytes, File(library(root), "arm64/libzte_saliency.so").readBytes())
            assertArrayEquals(maskBytes, File(library(root), "arm64/rme_aod_mask.raw").readBytes())
        }
    }

    @Test fun missingDamagedOrThrowingAssetsAndExistingLibAreVisibleButNonfatal() {
        for (kind in listOf("missing", "digest", "mask", "conflict", "wrong-apk")) {
            val (lease, root) = fixture()
            lease.use {
                if (kind == "conflict") File(library(root), "sentinel").apply { parentFile!!.mkdirs(); writeText("keep") }
                if (kind == "wrong-apk") apk(root).writeText("different")
                val result = prepare(lease, source = {
                    when (kind) {
                        "missing" -> throw IOException("missing asset")
                        "digest" -> inputs().copy(proxySha256 = "00".repeat(32))
                        "mask" -> inputs().copy(mask = byteArrayOf(0))
                        else -> inputs()
                    }
                })
                assertTrue("$kind must report unavailable", result is TargetAodImportStage.Result.Unavailable)
                if (kind == "conflict") assertEquals("keep", File(library(root), "sentinel").readText())
                else assertFalse("$kind modified library tree", library(root).exists())
            }
        }
    }

    @Test fun failedWriteIsNonfatalOnlyWhenOwnFilesAreRemoved() {
        val (lease, root) = fixture()
        lease.use {
            val failure = IOException("injected write failure")
            val result = prepare(lease, afterProxy = { throw failure })
            assertTrue(result is TargetAodImportStage.Result.Unavailable)
            assertSame(failure, (result as TargetAodImportStage.Result.Unavailable).cause)
            assertFalse(library(root).exists())
        }
    }

    @Test fun assetReadFailureIsPreservedForTheCaller() {
        val (lease, root) = fixture()
        lease.use {
            val failure = IOException("injected asset read failure")
            val result = prepare(lease, source = { throw failure })
            assertTrue(result is TargetAodImportStage.Result.Unavailable)
            assertSame(failure, (result as TargetAodImportStage.Result.Unavailable).cause)
            assertFalse(library(root).exists())
        }
    }

    @Test fun malformedAssetIndexIsReportedWithoutHidingItsCause() {
        val (lease, root) = fixture()
        lease.use {
            val failure = NumberFormatException("invalid asset index")
            val result = prepare(lease, source = { throw failure })
            assertTrue(result is TargetAodImportStage.Result.Unavailable)
            assertSame(failure, (result as TargetAodImportStage.Result.Unavailable).cause)
            assertFalse(library(root).exists())
        }
    }

    @Test fun failedCleanupAbortsInsteadOfPublishingAnUnknownPartialTree() {
        val (lease, root) = fixture()
        lease.use {
            val failure = assertThrows(IOException::class.java) {
                prepare(lease, afterProxy = { arm -> File(arm, "unrecognized").writeText("keep"); throw IOException("injected") })
            }
            assertTrue(failure.suppressed.isNotEmpty())
            assertEquals("keep", File(library(root), "arm64/unrecognized").readText())
        }
    }

    @Test fun cancellationIsNotTurnedIntoUnavailableAndOwnFilesAreRemoved() {
        val (lease, root) = fixture()
        lease.use {
            assertThrows(ImportCancelledException::class.java) {
                prepare(lease, afterProxy = { throw ImportCancelledException() })
            }
            assertFalse(library(root).exists())
        }
    }

    @Test fun cancellationDuringAssetReadPropagatesWithoutWriting() {
        val (lease, root) = fixture()
        lease.use {
            assertThrows(ImportCancelledException::class.java) {
                prepare(lease, source = { throw ImportCancelledException() })
            }
            assertFalse(library(root).exists())
        }
    }

    @Test fun importerLeavesOtherVariantsUnchangedAndDoesNotReadAssets() {
        val (lease, root) = fixture()
        lease.use {
            assertTrue(File(root, "dev/block/by-name/ztecfg").delete())
            val original = image()
            var read = false
            val logs = mutableListOf<String>()
            val result = applyAodImportPreparation(original, { prepare(lease, source = { read = true; inputs() }) }, logs::add)
            assertEquals(original, result)
            assertFalse(read)
            assertTrue(logs.isEmpty())
            assertFalse(library(root).exists())
        }
    }

    @Test fun importerAddsWarningOnlyWhenMatchingGuestCannotPrepareAssets() {
        val (lease, root) = fixture()
        lease.use {
            val original = image().copy(warnings = listOf("existing warning"))
            val logs = mutableListOf<String>()
            val result = applyAodImportPreparation(original, { prepare(lease, source = { throw IOException("missing") }) }, logs::add)
            assertEquals("existing warning", result.warnings.first())
            assertEquals(2, result.warnings.size)
            assertTrue(result.warnings.last().contains("AOD"))
            assertEquals(result.warnings.last(), logs.single())
            assertFalse(library(root).exists())
        }
    }

    @Test fun importerRecordsPreparedFilesWithoutClaimingGuestVerification() {
        val (lease, root) = fixture()
        lease.use {
            val original = image()
            val logs = mutableListOf<String>()
            val result = applyAodImportPreparation(original, { prepare(lease) }, logs::add)
            assertEquals(original, result)
            assertTrue(File(library(root), "arm64/libzte_saliency.so").isFile)
            assertTrue(File(library(root), "arm64/rme_aod_mask.raw").isFile)
            assertEquals(1, logs.size)
            assertTrue(logs.single().contains("prepared"))
            assertTrue(logs.single().contains("not yet verified"))
        }
    }

    @Test fun importerUsesEffectiveEngineForFreshAnalyzerImageAndKeepsTargetChecks() {
        val (lease, root) = fixture()
        lease.use {
            val analyzed = image().copy(engine = Engine.forApi(36))
            assertEquals(Engine.KK, analyzed.engine)
            assertEquals(Engine.N64, analyzed.effective().engine)
            val logs = mutableListOf<String>()
            val result = applyAodImportPreparation(analyzed, { prepare(lease, aodTargetForImport(analyzed)) }, logs::add)
            assertEquals(analyzed, result)
            assertTrue(File(library(root), "arm64/libzte_saliency.so").isFile)
            assertEquals(1, logs.size)
        }
        val (otherLease, otherRoot) = fixture()
        otherLease.use {
            val other = image().copy(engine = Engine.forApi(36), abi = "armeabi-v7a")
            var read = false
            val logs = mutableListOf<String>()
            val result = applyAodImportPreparation(other,
                { prepare(otherLease, aodTargetForImport(other), source = { read = true; inputs() }) }, logs::add)
            assertEquals(other, result)
            assertFalse(read)
            assertTrue(logs.isEmpty())
            assertFalse(library(otherRoot).exists())
        }
    }

    @Test fun importerDoesNotConvertCancellationOrFailedCleanupIntoWarning() {
        val (lease, root) = fixture()
        lease.use {
            val original = image()
            val logs = mutableListOf<String>()
            assertThrows(ImportCancelledException::class.java) {
                applyAodImportPreparation(original, { prepare(lease, source = { throw ImportCancelledException() }) }, logs::add)
            }
            assertThrows(IOException::class.java) {
                applyAodImportPreparation(original, {
                    prepare(lease, afterProxy = { arm -> File(arm, "unknown").writeText("keep"); throw IOException("injected") })
                }, logs::add)
            }
            assertTrue(logs.isEmpty())
            assertEquals("keep", File(library(root), "arm64/unknown").readText())
        }
    }
}
