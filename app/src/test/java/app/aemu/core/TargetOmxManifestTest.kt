package app.aemu.core

import java.io.File
import java.security.MessageDigest
import javax.xml.parsers.DocumentBuilderFactory
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Test

class TargetOmxManifestTest {
    @Test fun api36OwnVendorManifestsOmitUnavailableOmxOnly() {
        val root = File("src/main/assets/engines/n64-36-own")
        val expected = "f67cb2ab7e332e8ab89170d1484bf4474e24206c7c2fb2420b126484fcf28d18"
        for (name in listOf("vendor#etc#vintf#manifest.xml", "vendor#manifest.xml")) {
            val file = File(root, name)
            val bytes = file.readBytes()
            DocumentBuilderFactory.newInstance().newDocumentBuilder().parse(bytes.inputStream())
            assertFalse(String(bytes, Charsets.UTF_8).contains("android.hardware.media.omx"))
            val digest = MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") { "%02x".format(it.toInt() and 0xff) }
            assertEquals(expected, digest)
        }
    }
}
