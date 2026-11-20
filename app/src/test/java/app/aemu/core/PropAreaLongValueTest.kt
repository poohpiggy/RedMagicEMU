package app.aemu.core

import java.nio.ByteBuffer
import java.nio.ByteOrder
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class PropAreaLongValueTest {
    @get:Rule val temp = TemporaryFolder()

    @Test fun fingerprintUsesBionicLongValueLayout() {
        val file = temp.newFile()
        val key = "ro.system.build.fingerprint"
        val fingerprint = InstanceAdmission.INSTALLED_SYSTEM_FINGERPRINT
        PropArea.create(file, 36).use { area ->
            assertTrue(area.put(key, fingerprint))
        }
        PropArea.open(file)!!.use { area ->
            assertEquals(fingerprint, area.get(key))
            assertEquals(mapOf(key to fingerprint), area.all().toMap())
        }
        val bytes = file.readBytes()
        val entry = bytes.toString(Charsets.ISO_8859_1).indexOf(key) - 96
        val layout = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        assertTrue(layout.getInt(entry) and (1 shl 16) != 0)
        val value = entry + layout.getInt(entry + 60)
        assertEquals(fingerprint, bytes.decodeToString(value, value + fingerprint.length))
        assertEquals(0, bytes[value + fingerprint.length].toInt())
    }

    @Test fun bootCanRefreshAnExistingPropertyAndMutableValuesKeepTheShortFormat() {
        val key = "ro.system.build.fingerprint"
        val oldValue = InstanceAdmission.INSTALLED_SYSTEM_FINGERPRINT.take(91)
        val newValue = InstanceAdmission.INSTALLED_SYSTEM_FINGERPRINT
        PropArea.create(temp.newFile(), 36).use { area ->
            assertTrue(area.put(key, oldValue))
            assertTrue(area.put(key, newValue))
            assertEquals(newValue, area.get(key))
            assertTrue(area.put(key, oldValue))
            assertEquals(oldValue, area.get(key))
            assertTrue(area.put("debug.ota", newValue))
            assertEquals(oldValue, area.get("debug.ota"))
        }
    }
}
