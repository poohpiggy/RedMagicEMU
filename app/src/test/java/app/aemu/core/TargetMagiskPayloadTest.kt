package app.aemu.core

import org.junit.Assert.*
import org.junit.Test
import org.junit.Rule
import org.junit.rules.TemporaryFolder
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder

class TargetMagiskPayloadTest {
    @get:Rule val folder = TemporaryFolder()

    @Test fun preinitStoragePersistsPerInstanceAndRejectsVersionMismatch() {
        val first = folder.newFolder("first")
        val second = folder.newFolder("second")
        val storage = TargetMagiskPayload.preparePreinit(first)
        val other = TargetMagiskPayload.preparePreinit(second)
        assertEquals(File(first, "data/adb/rme-magisk-preinit"), storage)
        assertEquals(File(second, "data/adb/rme-magisk-preinit"), other)
        val version = File(storage, "version")
        assertEquals("31100\n", version.readText())
        assertEquals(storage, TargetMagiskPayload.preparePreinit(first))
        version.writeText("old\n")
        assertThrows(IllegalStateException::class.java) { TargetMagiskPayload.preparePreinit(first) }
        assertEquals("old\n", version.readText())
        assertEquals("31100\n", File(other, "version").readText())
    }

    @Test fun preinitStorageDoesNotReplaceAnExistingFile() {
        val root = folder.newFolder()
        val data = File(root, "data").apply { mkdir() }
        val collision = File(data, "adb").apply { writeText("existing") }
        assertThrows(IllegalStateException::class.java) { TargetMagiskPayload.preparePreinit(root) }
        assertEquals("existing", collision.readText())
    }

    @Test fun stockImageIsIntactAndRuntimeCopyUsesTheInstanceInterpreter() {
        val stock = stockImage()
        val original = stock.copyOf()
        val interpreter = "/data/app/redmagicemu/lib/arm64/libaemu64_36.so"
        val patched = TargetMagiskPayload.withInterpreter(stock, interpreter)
        val header = ByteBuffer.wrap(patched).order(ByteOrder.LITTLE_ENDIAN)
        assertArrayEquals(original, stock)
        assertEquals(stock.size.toLong(), header.getLong(64 + 8))
        assertEquals(interpreter.length.toLong() + 1, header.getLong(64 + 32))
        assertEquals(interpreter.length.toLong() + 1, header.getLong(64 + 40))
        assertEquals(interpreter, String(patched, stock.size, interpreter.length, Charsets.US_ASCII))
        assertEquals(0.toByte(), patched.last())
    }

    @Test fun rejectsNonStockInterpreterAndTruncatedElf() {
        val modified = stockImage().apply { this[120] = 'x'.code.toByte() }
        assertThrows(IllegalArgumentException::class.java) {
            TargetMagiskPayload.withInterpreter(modified, "/runner")
        }
        assertThrows(IllegalArgumentException::class.java) {
            TargetMagiskPayload.withInterpreter(stockImage().copyOf(70), "/runner")
        }
    }

    @Test fun magiskIsOptInForEachInstance() {
        val original = VmSettings()
        val enabled = original.copy(magisk = true)
        assertFalse(original.magisk)
        assertTrue(enabled.magisk)
        assertTrue(enabled.copy(vulkan = false).magisk)
        assertEquals(original, enabled.copy(magisk = false))
    }

    private fun stockImage() = ByteArray(200).apply {
        byteArrayOf(0x7f, 0x45, 0x4c, 0x46, 2, 1).copyInto(this)
        val data = ByteBuffer.wrap(this).order(ByteOrder.LITTLE_ENDIAN)
        data.putShort(18, 183)
        data.putLong(32, 64)
        data.putShort(54, 56)
        data.putShort(56, 1)
        val stock = "/system/bin/linker64\u0000".toByteArray(Charsets.US_ASCII)
        data.putInt(64, 3)
        data.putLong(64 + 8, 120)
        data.putLong(64 + 32, stock.size.toLong())
        data.putLong(64 + 40, stock.size.toLong())
        stock.copyInto(this, 120)
    }
}
