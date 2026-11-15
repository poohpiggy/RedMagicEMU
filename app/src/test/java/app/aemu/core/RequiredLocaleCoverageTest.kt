package app.aemu.core

import app.aemu.AppPrefs
import java.io.File
import javax.xml.parsers.DocumentBuilderFactory
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.w3c.dom.Element

class RequiredLocaleCoverageTest {
    private val required = linkedMapOf(
        "en-US" to "values-en-rUS",
        "en-GB" to "values-en-rGB",
        "zh-CN" to "values-zh-rCN",
        "zh-TW" to "values-zh-rTW",
        "zh-HK" to "values-zh-rHK",
    )
    private val placeholder = Regex("%(?:\\d+\\$)?[sd]|%%")

    private fun strings(directory: String): Map<String, String> {
        val file = File("src/main/res/$directory/strings.xml")
        val root = DocumentBuilderFactory.newInstance().newDocumentBuilder().parse(file).documentElement
        val nodes = root.getElementsByTagName("string")
        return buildMap {
            for (index in 0 until nodes.length) {
                val element = nodes.item(index) as Element
                val name = element.getAttribute("name")
                if (name != "app_name") put(name, element.textContent)
            }
        }
    }

    @Test fun requiredLocalesAreCompleteAndPlaceholderCompatible() {
        val base = strings("values")
        for ((tag, directory) in required) {
            val translations = strings(directory)
            assertEquals("$tag keys", base.keys, translations.keys)
            for ((key, value) in translations) {
                assertTrue("$tag/$key is empty", value.isNotBlank())
                assertEquals(
                    "$tag/$key placeholders",
                    placeholder.findAll(base.getValue(key)).map { it.value }.sorted().toList(),
                    placeholder.findAll(value).map { it.value }.sorted().toList(),
                )
            }
            assertTrue("$tag missing from language picker", AppPrefs.LANGUAGES.any { it.first == tag })
        }

        val localeFile = File("src/main/res/xml/locales_config.xml")
        val locales = DocumentBuilderFactory.newInstance().newDocumentBuilder().parse(localeFile).documentElement
        val configured = (0 until locales.getElementsByTagName("locale").length)
            .map { locales.getElementsByTagName("locale").item(it) as Element }
            .map { it.getAttribute("android:name") }
            .toSet()
        assertEquals(required.keys, configured)
        assertEquals(setOf("") + required.keys, AppPrefs.LANGUAGES.map { it.first }.toSet())
    }
}
