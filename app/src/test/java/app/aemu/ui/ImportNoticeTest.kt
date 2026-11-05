package app.aemu.ui

import org.junit.Assert.assertEquals
import org.junit.Test

class ImportNoticeTest {
    @Test fun hidesObsoleteEngineAdviceButPreservesOtherWarnings() {
        val obsolete = "64-bit firmware: the native arm64 engine is tuned for Android 6.0; other versions are experimental."
        val other = "Android 16: support is experimental (ART, SELinux)."
        assertEquals(listOf(other), visibleImportWarnings(listOf(obsolete, other)))
    }
}
