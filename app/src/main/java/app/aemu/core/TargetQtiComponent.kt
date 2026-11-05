package app.aemu.core

import java.io.File
import java.io.IOException
import java.nio.ByteBuffer
import java.nio.channels.FileChannel
import java.nio.file.FileAlreadyExistsException
import java.nio.file.Files
import java.nio.file.LinkOption.NOFOLLOW_LINKS
import java.nio.file.Path
import java.nio.file.StandardOpenOption.READ
import java.nio.file.StandardOpenOption.WRITE

/** Default-disable the QTI extension that requires the quarantined QTI radio HAL. */
internal object TargetQtiComponent {
    private const val CONFIG = "aemu-nx809j-qti.xml"
    private const val DATA_PREFIX = ".aemu-nx809j-qti-"
    private val content = """<?xml version="1.0" encoding="utf-8"?>
<config>
    <component-override package="com.qti.phone">
        <component class="com.qti.phone.ExtTelephonyService" enabled="false" />
    </component-override>
</config>
""".toByteArray(Charsets.UTF_8)

    fun prepare(root: File, api: Int, model: String, engine: Engine): Boolean {
        if (api != 36 || model != "NX809J" || engine != Engine.N64) return false
        var parent = root.toPath()
        if (!Files.isDirectory(parent, NOFOLLOW_LINKS)) throw IOException("Invalid firmware root")
        for (part in listOf("system", "etc", "sysconfig")) {
            parent = parent.resolve(part)
            try { Files.createDirectory(parent) } catch (_: FileAlreadyExistsException) { }
            if (!Files.isDirectory(parent, NOFOLLOW_LINKS)) throw IOException("Unsafe sysconfig directory: $part")
        }
        return publish(parent) { file ->
            val bytes = ByteBuffer.wrap(content)
            while (bytes.hasRemaining()) file.write(bytes)
        }
    }

    /** Publish only after the complete data file has been synced. Existing targets are never replaced. */
    internal fun publish(parent: Path, write: (FileChannel) -> Unit): Boolean {
        val target = parent.resolve(CONFIG)
        if (Files.exists(target, NOFOLLOW_LINKS)) return verifyExisting(target)
        val data = Files.createTempFile(parent, DATA_PREFIX, ".tmp")
        var published = false
        try {
            FileChannel.open(data, WRITE, NOFOLLOW_LINKS).use { file ->
                write(file)
                file.force(true)
            }
            verifyData(data)
            if (Files.exists(target, NOFOLLOW_LINKS)) return verifyExisting(target)
            try {
                // createSymbolicLink is atomic and fails when the name already exists.
                Files.createSymbolicLink(target, data.fileName)
                published = true
                return true
            } catch (_: FileAlreadyExistsException) {
                return verifyExisting(target)
            }
        } finally {
            if (!published) Files.deleteIfExists(data)
        }
    }

    private fun verifyExisting(target: Path): Boolean {
        val file = if (Files.isSymbolicLink(target)) {
            val link = Files.readSymbolicLink(target)
            val name = link.toString()
            if (link.isAbsolute || link.nameCount != 1 || !name.startsWith(DATA_PREFIX) ||
                !name.endsWith(".tmp") || !name.matches(Regex("[A-Za-z0-9._-]{1,128}")))
                throw IOException("Unsafe QTI component override link")
            target.parent.resolve(link)
        } else target
        verifyData(file)
        return false
    }

    private fun verifyData(file: Path) {
        if (!Files.isRegularFile(file, NOFOLLOW_LINKS)) throw IOException("Conflicting QTI component override")
        FileChannel.open(file, READ, NOFOLLOW_LINKS).use { input ->
            if (input.size() != content.size.toLong()) throw IOException("Conflicting QTI component override")
            val bytes = ByteBuffer.allocate(content.size)
            while (bytes.hasRemaining()) if (input.read(bytes) < 0) throw IOException("Incomplete QTI component override")
            if (!bytes.array().contentEquals(content) || input.read(ByteBuffer.allocate(1)) != -1)
                throw IOException("Conflicting QTI component override")
        }
    }
}
