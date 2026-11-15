package app.aemu.core

import app.aemu.AppPrefs
import org.junit.Assert.assertEquals
import org.junit.Test

class LanguageFallbackTest {
    @Test fun supportedExplicitSelectionsAreKept() {
        for (tag in listOf("en-US", "en-GB", "zh-CN", "zh-TW", "zh-HK")) {
            assertEquals(tag, AppPrefs.resolveLanguageTag(tag, "fr-FR"))
        }
    }

    @Test fun followSystemUsesSupportedPrimaryLocale() {
        assertEquals("en-GB", AppPrefs.resolveLanguageTag("", "en-GB"))
        assertEquals("zh-CN", AppPrefs.resolveLanguageTag("", "zh-CN"))
        assertEquals("zh-TW", AppPrefs.resolveLanguageTag("", "zh-TW"))
        assertEquals("zh-HK", AppPrefs.resolveLanguageTag("", "zh-HK"))
    }

    @Test fun unsupportedLocalesDefaultToUsEnglish() {
        assertEquals("en-US", AppPrefs.resolveLanguageTag("", "fr-FR"))
        assertEquals("en-US", AppPrefs.resolveLanguageTag("", "ja-JP"))
        assertEquals("en-US", AppPrefs.resolveLanguageTag("", ""))
        assertEquals("en-US", AppPrefs.resolveLanguageTag("de-DE", "zh-CN"))
        assertEquals("en-US", AppPrefs.resolveLanguageTag("en", "zh-CN"))
    }

    @Test fun localeExtensionsDoNotPreventMatching() {
        assertEquals("en-US", AppPrefs.resolveLanguageTag("", "en-US-u-nu-latn"))
        assertEquals("zh-HK", AppPrefs.resolveLanguageTag("", "zh-Hant-HK"))
    }
}
