package app.aemu.core

import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File
import java.io.RandomAccessFile
import java.nio.file.Files

class TargetCarrierPropertiesTest {
    @get:Rule val temp = TemporaryFolder()
    private fun guest() = temp.newFolder()
    private fun project(root: File, name: String, content: String) {
        File(root, "system/carrier/projects/$name/prop_fixed.txt").apply { parentFile!!.mkdirs(); writeText(content) }
    }
    private fun cfg(root: File, records: List<Pair<String, String>>) {
        val f = File(root, "dev/block/by-name/ztecfg").apply { parentFile!!.mkdirs(); writeBytes(ByteArray(524288)) }
        RandomAccessFile(f, "rw").use { io ->
            records.forEachIndexed { i, (key, value) ->
                io.seek(4096L + i * 128)
                listOf("this-is-cfg-head", key, "cfg-signature-is-here", value).forEach { field ->
                    val bytes = field.toByteArray(Charsets.US_ASCII); require(bytes.size < 32)
                    io.write(bytes); io.write(ByteArray(32 - bytes.size))
                }
            }
        }
    }
    private fun load(root: File) = TargetCarrierProperties.load(root, 36, "NX809J", "arm64-v8a")

    @Test fun choosesFirstMatchingVariantAndProjectProperties() {
        val root = guest(); cfg(root, listOf("other" to "ignore", "zte_variantflag" to "IP_MC_W_CN", "zte_variantflag" to "GEN_CN"))
        project(root, "IP_MC_W_CN", "ro.vendor.build.def_theme_name=first\nro.vendor.build.def_theme_name=second\npersist.vendor.custom.variant.id=IP_MC_W_CN\n")
        project(root, "GEN_CN", "ro.vendor.build.def_theme_name=default\n")
        val loaded = load(root) as TargetCarrierProperties.Result.Loaded
        assertFalse(loaded.fallback)
        assertEquals("second", loaded.properties["ro.vendor.build.def_theme_name"])
        assertEquals("IP_MC_W_CN", loaded.properties["persist.vendor.custom.variant.id"])
    }
    @Test fun missingImageDoesNotChangeExistingBoot() {
        assertEquals(TargetCarrierProperties.Result.MissingImage, load(guest()))
        assertEquals(TargetCarrierProperties.Result.NotTarget, TargetCarrierProperties.load(guest(), 35, "NX809J", "arm64-v8a"))
    }
    @Test fun missingVariantOrProjectFallsBackToGeneric() {
        val root = guest(); project(root, "GEN_CN", "ro.vendor.build.def_theme_name=generic\n")
        cfg(root, listOf("different" to "ignored"))
        var loaded = load(root) as TargetCarrierProperties.Result.Loaded
        assertTrue(loaded.fallback); assertEquals("GEN_CN", loaded.variant); assertEquals("generic", loaded.properties["ro.vendor.build.def_theme_name"])
        cfg(root, listOf("zte_variantflag" to "UNAVAILABLE"))
        loaded = load(root) as TargetCarrierProperties.Result.Loaded
        assertTrue(loaded.fallback); assertEquals("generic", loaded.properties["ro.vendor.build.def_theme_name"])
    }
    @Test fun rejectsMalformedImageAndUnsafeVariantWithoutReadingOutsideRoot() {
        val root = guest(); cfg(root, listOf("zte_variantflag" to "../outside"))
        project(root, "GEN_CN", "ro.vendor.build.def_theme_name=generic\n")
        assertTrue(load(root) is TargetCarrierProperties.Result.Invalid)
        File(root, "dev/block/by-name/ztecfg").writeBytes(ByteArray(1024))
        assertTrue(load(root) is TargetCarrierProperties.Result.Invalid)
    }
    @Test fun unsupportedProjectPropertyIsReportedInsteadOfApplied() {
        val root = guest(); cfg(root, listOf("zte_variantflag" to "IP_MC_W_CN"))
        project(root, "IP_MC_W_CN", "ro.secure=0\n")
        assertEquals("unsupported project property", (load(root) as TargetCarrierProperties.Result.Invalid).reason)
    }
    @Test fun otherFirmwareVariantAcceptsItsOwnProductNameProperty() {
        val root = guest(); cfg(root, listOf("zte_variantflag" to "IP_PB_CN"))
        project(root, "IP_PB_CN", "ro.vendor.product.ztename=fixture\n")
        val loaded = load(root) as TargetCarrierProperties.Result.Loaded
        assertEquals("fixture", loaded.properties["ro.vendor.product.ztename"])
    }
    @Test fun malformedFieldAndPropertyValueAreReported() {
        val root = guest(); cfg(root, listOf("zte_variantflag" to "IP_MC_W_CN"))
        project(root, "IP_MC_W_CN", "ro.vendor.build.def_theme_name=foo\u0000bar\n")
        assertTrue(load(root) is TargetCarrierProperties.Result.Invalid)
        RandomAccessFile(File(root, "dev/block/by-name/ztecfg"), "rw").use { io ->
            io.seek(4096L + 96); io.write(ByteArray(32) { 'A'.code.toByte() })
        }
        assertTrue(load(root) is TargetCarrierProperties.Result.Invalid)
    }
    @Test fun invalidRecordIsNotInterpretedAsVariant() {
        val root = guest(); cfg(root, listOf("zte_variantflag" to "IP_MC_W_CN"))
        RandomAccessFile(File(root, "dev/block/by-name/ztecfg"), "rw").use { io -> io.seek(4096L + 64); io.writeByte(0) }
        project(root, "GEN_CN", "ro.vendor.build.def_theme_name=generic\n")
        assertTrue((load(root) as TargetCarrierProperties.Result.Loaded).fallback)
    }
    @Test fun projectPrecedesSavedPersistentAndRuntimeOverrides() {
        PropArea.create(temp.newFile(), 36).use { area ->
            area.put("ro.vendor.build.def_theme_name", "template")
            applyBootPropertyLayers(area, mapOf("ro.vendor.build.def_theme_name" to "ordinary", "ordinary" to "v"),
                mapOf("ro.vendor.build.def_theme_name" to "project", "persist.vendor.custom.variant.id" to "project", "ro.config.ringtone" to "project"),
                mapOf("persist.vendor.custom.variant.id" to "saved"), mapOf("ro.config.ringtone" to "runtime"))
            assertEquals("project", area.get("ro.vendor.build.def_theme_name")); assertEquals("saved", area.get("persist.vendor.custom.variant.id"))
            assertEquals("runtime", area.get("ro.config.ringtone")); assertEquals("v", area.get("ordinary"))
        }
    }
    @Test fun rejectsMismatchBetweenSelectedProjectAndEffectiveVariantBeforeFramework() {
        val root = guest(); cfg(root, listOf("zte_variantflag" to "IP_MC_W_CN"))
        project(root, "IP_MC_W_CN", "persist.vendor.custom.variant.id=IP_MC_W_CN\n")
        val selected = load(root) as TargetCarrierProperties.Result.Loaded
        assertEquals("IP_MC_W_CN", selected.variant)
        TargetCarrierProperties.requireVariantMatches(selected.variant, "IP_MC_W_CN")
        for (observed in listOf(null, "GEN_CN", "")) {
            try { TargetCarrierProperties.requireVariantMatches(selected.variant, observed); fail("mismatched variant accepted") }
            catch (_: IllegalStateException) { }
        }
    }
    @Test fun rejectsLinksOnProjectPathWhereSupported() {
        val root = guest(); cfg(root, listOf("zte_variantflag" to "IP_MC_W_CN"))
        val dir = File(root, "system/carrier/projects"); dir.mkdirs()
        val outside = temp.newFolder(); File(outside, "prop_fixed.txt").writeText("ro.vendor.build.def_theme_name=outside\n")
        try { Files.createSymbolicLink(File(dir, "IP_MC_W_CN").toPath(), outside.toPath()) }
        catch (e: Exception) { org.junit.Assume.assumeNoException(e) }
        assertTrue(load(root) is TargetCarrierProperties.Result.Invalid)
    }
}
