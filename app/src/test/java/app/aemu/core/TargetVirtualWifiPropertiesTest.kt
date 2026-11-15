package app.aemu.core

import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.ByteArrayInputStream
import java.io.IOException
import java.nio.file.Files
import java.nio.file.attribute.PosixFilePermission
import java.util.Properties

class TargetVirtualWifiPropertiesTest {
    @get:Rule val temp = TemporaryFolder()

    @Test fun encodedPropertiesRoundTripThroughGuestReader() {
        val config = TargetVirtualWifiConfig(" 空间=\\#", -73)
        val result = Properties().apply { load(ByteArrayInputStream(TargetVirtualWifiProperties.encode(config))) }
        assertEquals(config.ssid, result.getProperty("ssid"))
        assertFalse(result.containsKey("psk"))
        assertEquals("false", result.getProperty("secured"))
        assertEquals("-73", result.getProperty("rssi"))
        assertArrayEquals(TargetVirtualWifiProperties.encode(config), TargetVirtualWifiProperties.encode(config))
        val open = Properties().apply { load(ByteArrayInputStream(TargetVirtualWifiProperties.encode(TargetVirtualWifiConfig()))) }
        assertEquals("false", open.getProperty("secured"))
        assertFalse(open.containsKey("psk"))
    }

    @Test fun refusesInvalidConfigBeforeWriting() {
        val root = temp.newFolder("invalid")
        try { TargetVirtualWifiProperties.prepareForStoppedGuest(root, TargetVirtualWifiConfig(ssid = "")); fail() }
        catch (_: IOException) { }
        assertFalse(root.resolve("data/system/aemu-wifi.properties").exists())
    }

    @Test fun writesPrivateFileUpdatesOwnedConfigAndRefusesForeignContent() {
        val root = temp.newFolder("guest")
        val dir = root.resolve("data/system").apply { assertTrue(mkdirs()) }
        assumeTrue(Files.getFileStore(dir.toPath()).supportsFileAttributeView("posix"))
        val first = TargetVirtualWifiConfig()
        TargetVirtualWifiProperties.prepareForStoppedGuest(root, first)
        val file = dir.resolve("aemu-wifi.properties")
        assertEquals(setOf(PosixFilePermission.OWNER_READ, PosixFilePermission.OWNER_WRITE), Files.getPosixFilePermissions(file.toPath()))
        val bytes = file.readBytes()
        TargetVirtualWifiProperties.prepareForStoppedGuest(root, first)
        val updated = first.copy(ssid = "different")
        TargetVirtualWifiProperties.prepareForStoppedGuest(root, updated)
        assertArrayEquals(TargetVirtualWifiProperties.encode(updated), file.readBytes())
        val foreign = "unrelated firmware data".toByteArray()
        file.writeBytes(foreign)
        try { TargetVirtualWifiProperties.prepareForStoppedGuest(root, first); fail() }
        catch (_: IOException) { }
        assertArrayEquals(foreign, file.readBytes())
    }

    @Test fun ownerValidationRejectsMalformedAndForeignProperties() {
        val bytes = TargetVirtualWifiProperties.encode(TargetVirtualWifiConfig())
        assertTrue(TargetVirtualWifiProperties.isOwned(bytes))
        assertFalse(TargetVirtualWifiProperties.isOwned("ssid=foreign\n".toByteArray()))
        assertFalse(TargetVirtualWifiProperties.isOwned(bytes + "other=value\n".toByteArray()))
        assertFalse(TargetVirtualWifiProperties.isOwned(bytes + "ssid=changed\n".toByteArray()))
    }

    @Test fun recognizesPreviousOwnedSecuredConfigurationForOpenMigration() {
        val old = ("# RedMagicEMU virtual Wi-Fi configuration v1\n" +
            "ssid=OldName\nsecured=true\npsk=example-old-password\nrssi=-53\n").toByteArray()
        assertTrue(TargetVirtualWifiProperties.isOwned(old))
        val migrated = Properties().apply { load(ByteArrayInputStream(TargetVirtualWifiProperties.encode(TargetVirtualWifiConfig("OldName", -53)))) }
        assertEquals("false", migrated.getProperty("secured"))
        assertFalse(migrated.containsKey("psk"))
    }

    @Test fun racedForeignFileIsNeverReplaced() {
        val root = temp.newFolder("race")
        val dir = root.resolve("data/system").apply { assertTrue(mkdirs()) }
        assumeTrue(Files.getFileStore(dir.toPath()).supportsFileAttributeView("posix"))
        val file = dir.resolve("aemu-wifi.properties")
        val foreign = "foreign".toByteArray()
        try {
            TargetVirtualWifiProperties.prepareForStoppedGuest(root, TargetVirtualWifiConfig()) {
                Files.write(file.toPath(), foreign)
            }
            fail()
        } catch (_: IOException) { }
        assertArrayEquals(foreign, file.readBytes())
    }

    @Test fun refusesLinkedRuntimeDirectory() {
        val root = temp.newFolder("linked")
        val outside = temp.newFolder("outside")
        try { Files.createSymbolicLink(root.resolve("data").toPath(), outside.toPath()) }
        catch (e: Exception) { org.junit.Assume.assumeNoException(e) }
        try { TargetVirtualWifiProperties.prepareForStoppedGuest(root, TargetVirtualWifiConfig()); fail() }
        catch (_: IOException) { }
        assertTrue(outside.listFiles()!!.isEmpty())
    }
}
