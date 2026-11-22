package app.aemu.core

import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.file.Files
import java.nio.file.LinkOption.NOFOLLOW_LINKS
import java.nio.file.StandardCopyOption.ATOMIC_MOVE

internal object TargetMagiskPayload {
    const val VERSION = "31.1"
    const val VERSION_CODE = "31100"
    const val MANAGER_PACKAGE = "app.redmagicemu.magisk"

    fun preparePreinit(root: File): File {
        val directory = listOf("data", "adb", "rme-magisk-preinit").fold(root) { parent, name ->
            File(parent, name).also { child ->
                if (!Files.exists(child.toPath(), NOFOLLOW_LINKS)) Files.createDirectory(child.toPath())
                check(Files.isDirectory(child.toPath(), NOFOLLOW_LINKS)) { "Unexpected Guest Magisk directory: $name" }
            }
        }
        val version = File(directory, "version").toPath()
        val expected = "$VERSION_CODE\n".toByteArray(Charsets.US_ASCII)
        if (!Files.exists(version, NOFOLLOW_LINKS)) {
            val staged = Files.createTempFile(directory.toPath(), ".version-", ".tmp")
            try {
                Files.write(staged, expected)
                Files.move(staged, version, ATOMIC_MOVE)
            } finally {
                Files.deleteIfExists(staged)
            }
        }
        check(Files.isRegularFile(version, NOFOLLOW_LINKS) &&
            Files.size(version) == expected.size.toLong() && Files.readAllBytes(version).contentEquals(expected)) {
            "Unexpected Guest Magisk preinit version"
        }
        return directory
    }

    private val elf = byteArrayOf(0x7f, 0x45, 0x4c, 0x46, 2, 1)
    private val stock = "/system/bin/linker64\u0000".toByteArray(Charsets.US_ASCII)

    fun withInterpreter(bytes: ByteArray, interpreter: String): ByteArray {
        require(bytes.size >= 64 && bytes.copyOf(elf.size).contentEquals(elf)) { "Invalid Magisk ELF" }
        require(interpreter.startsWith("/") && interpreter.length < 255 &&
            interpreter.all { it.code in 32..126 }) { "Invalid Guest interpreter path" }
        val image = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        require(image.getShort(18).toInt() == 183 && image.getShort(54).toInt() == 56)
        val headers = image.getLong(32)
        val count = image.getShort(56).toInt()
        require(headers in 64L..bytes.size.toLong() && count in 1..64 &&
            headers + 56L * count <= bytes.size) { "Invalid Magisk program headers" }
        val header = List(count) { headers.toInt() + 56 * it }.filter { image.getInt(it) == 3 }
            .singleOrNull() ?: throw IllegalArgumentException("Missing unique Magisk interpreter")
        val offset = image.getLong(header + 8)
        require(offset in 0L..(bytes.size - stock.size).toLong() &&
            image.getLong(header + 32) == stock.size.toLong() &&
            bytes.copyOfRange(offset.toInt(), offset.toInt() + stock.size).contentEquals(stock)) {
            "Unexpected Magisk interpreter"
        }
        val path = (interpreter + "\u0000").toByteArray(Charsets.US_ASCII)
        val patched = bytes + path
        ByteBuffer.wrap(patched).order(ByteOrder.LITTLE_ENDIAN).apply {
            putLong(header + 8, bytes.size.toLong())
            putLong(header + 32, path.size.toLong())
            putLong(header + 40, path.size.toLong())
        }
        return patched
    }
}
