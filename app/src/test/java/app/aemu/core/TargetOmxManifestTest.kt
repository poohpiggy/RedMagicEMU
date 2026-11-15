package app.aemu.core

import java.io.File
import javax.xml.parsers.DocumentBuilderFactory
import org.junit.Assert.assertFalse
import org.junit.Test

class TargetOmxManifestTest {
    @Test fun api36OwnVendorManifestsOmitUnavailableOmxOnly() {
        val root = File("src/main/assets/engines/n64-36-own")
        for (name in listOf("vendor#etc#vintf#manifest.xml", "vendor#manifest.xml")) {
            val file = File(root, name)
            val bytes = file.readBytes()
            DocumentBuilderFactory.newInstance().newDocumentBuilder().parse(bytes.inputStream())
            assertFalse(String(bytes, Charsets.UTF_8).contains("android.hardware.media.omx"))
        }
    }
}
