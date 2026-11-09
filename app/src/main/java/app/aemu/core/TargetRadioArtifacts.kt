package app.aemu.core

import java.io.File
import java.io.IOException
import java.nio.ByteBuffer
import java.nio.channels.FileChannel
import java.nio.file.FileAlreadyExistsException
import java.nio.file.Files
import java.nio.file.LinkOption.NOFOLLOW_LINKS
import java.nio.file.Path
import java.nio.file.StandardCopyOption.ATOMIC_MOVE
import java.nio.file.StandardCopyOption.REPLACE_EXISTING
import java.nio.file.StandardOpenOption.TRUNCATE_EXISTING
import java.nio.file.StandardOpenOption.WRITE
import java.util.zip.ZipFile

/** Files installed only in the selected firmware's stopped runtime copy. */
internal object TargetRadioArtifacts {
    private const val MANIFEST_ASSET = "engines/redmagic/radio-manifest.xml"
    private const val JAR_ASSET = "engines/redmagic/radio-service.jar"
    const val MANIFEST_NAME = "aemu-redmagic-radio.xml"
    private const val JAR_NAME = "redmagic-radio.jar"
    private const val IWLAN_OVERRIDE_NAME = "aemu-redmagic-iwlan.xml"
    private val iwlanOverride = """<?xml version="1.0" encoding="utf-8"?>
<config>
    <component-override package="vendor.qti.iwlan">
        <component class="vendor.qti.iwlan.IWlanNetworkService" enabled="false" />
    </component-override>
</config>
""".toByteArray(Charsets.UTF_8)
    private const val OWNER_ENTRY = "META-INF/redmagic-radio-owner"
    private val owner = "RedMagicEMU radio-service v1\n".toByteArray(Charsets.UTF_8)
    private const val MAX_JAR_SIZE = 32L * 1024 * 1024
    private const val MAX_MANIFEST_SIZE = 64L * 1024

    fun applies(api: Int, model: String, abi: String, engine: Engine): Boolean =
        api == 36 && model == "NX809J" && abi == "arm64-v8a" && engine == Engine.N64

    fun prepare(root: File, api: Int, model: String, abi: String, engine: Engine,
                enabled: Boolean, readAsset: (String) -> ByteArray) {
        if (!applies(api, model, abi, engine)) return
        val base = root.toPath()
        if (!Files.isDirectory(base, NOFOLLOW_LINKS)) throw IOException("Unsafe radio runtime root")
        val manifestDir = directory(base, listOf("vendor", "etc", "vintf", "manifest"), enabled)
        if (!enabled) {
            val manifest = manifestDir?.resolve(MANIFEST_NAME)
            val configDir = directory(base, listOf("system", "etc", "sysconfig"), false)
            val component = configDir?.resolve(IWLAN_OVERRIDE_NAME)
            val removeManifest = manifest != null && Files.exists(manifest, NOFOLLOW_LINKS)
            val removeComponent = component != null && Files.exists(component, NOFOLLOW_LINKS)
            if (removeManifest)
                requireContent(manifest!!, readAsset(MANIFEST_ASSET), MAX_MANIFEST_SIZE, "radio declaration")
            if (removeComponent)
                requireContent(component!!, iwlanOverride, MAX_MANIFEST_SIZE, "IWLAN component override")
            if (removeManifest) Files.delete(manifest!!)
            if (removeComponent) Files.delete(component!!)
            return
        }
        checkNotNull(manifestDir)
        val manifest = manifestDir.resolve(MANIFEST_NAME)
        val declaration = readAsset(MANIFEST_ASSET)
        val jarBytes = readAsset(JAR_ASSET)
        if (declaration.isEmpty() || declaration.size.toLong() > MAX_MANIFEST_SIZE ||
            jarBytes.isEmpty() || jarBytes.size.toLong() > MAX_JAR_SIZE)
            throw IOException("Invalid radio asset size")
        // A foreign declaration must never be replaced, even if the JAR is missing.
        if (Files.exists(manifest, NOFOLLOW_LINKS)) requireContent(manifest, declaration, MAX_MANIFEST_SIZE, "radio declaration")
        val configDir = directory(base, listOf("system", "etc", "sysconfig"), true)!!
        val component = configDir.resolve(IWLAN_OVERRIDE_NAME)
        if (Files.exists(component, NOFOLLOW_LINKS))
            requireContent(component, iwlanOverride, MAX_MANIFEST_SIZE, "IWLAN component override")
        val framework = directory(base, listOf("system", "framework"), true)!!
        val jar = framework.resolve(JAR_NAME)
        if (Files.exists(jar, NOFOLLOW_LINKS)) {
            requireRegular(jar, MAX_JAR_SIZE, "radio JAR")
            if (!Files.readAllBytes(jar).contentEquals(jarBytes) && !isOwnedJar(jar))
                throw IOException("Conflicting radio JAR")
        }
        installJar(jar, jarBytes)
        if (!Files.exists(manifest, NOFOLLOW_LINKS)) writeNew(manifest, declaration, manifestDir.parent)
        else requireContent(manifest, declaration, MAX_MANIFEST_SIZE, "radio declaration")
        if (!Files.exists(component, NOFOLLOW_LINKS)) writeNew(component, iwlanOverride, configDir.parent)
        else requireContent(component, iwlanOverride, MAX_MANIFEST_SIZE, "IWLAN component override")
    }

    private fun directory(root: Path, parts: List<String>, create: Boolean): Path? {
        var dir = root
        for (part in parts) {
            dir = dir.resolve(part)
            if (!Files.exists(dir, NOFOLLOW_LINKS)) {
                if (!create) return null
                try { Files.createDirectory(dir) } catch (_: FileAlreadyExistsException) { }
            }
            if (!Files.isDirectory(dir, NOFOLLOW_LINKS)) throw IOException("Unsafe radio directory: $part")
        }
        return dir
    }

    private fun requireRegular(path: Path, limit: Long, label: String) {
        if (!Files.isRegularFile(path, NOFOLLOW_LINKS) || Files.size(path) > limit)
            throw IOException("Conflicting $label")
    }

    private fun requireContent(path: Path, expected: ByteArray, limit: Long, label: String) {
        requireRegular(path, limit, label)
        if (!Files.readAllBytes(path).contentEquals(expected)) throw IOException("Conflicting $label")
    }

    private fun isOwnedJar(path: Path): Boolean = try {
        ZipFile(path.toFile()).use { zip ->
            val entries = zip.entries().asSequence().filter { it.name == OWNER_ENTRY }.toList()
            if (entries.size != 1 || entries[0].isDirectory || entries[0].size != owner.size.toLong()) return false
            zip.getInputStream(entries[0]).use { input ->
                val actual = ByteArray(owner.size + 1)
                var at = 0
                while (at < actual.size) {
                    val n = input.read(actual, at, actual.size - at)
                    if (n < 0) break
                    if (n == 0) throw IOException("Cannot read radio JAR marker")
                    at += n
                }
                at == owner.size && actual.copyOf(at).contentEquals(owner)
            }
        }
    } catch (_: java.util.zip.ZipException) { false }

    private fun writeBytes(path: Path, bytes: ByteArray) {
        FileChannel.open(path, WRITE, TRUNCATE_EXISTING, NOFOLLOW_LINKS).use { channel ->
            val data = ByteBuffer.wrap(bytes)
            while (data.hasRemaining()) if (channel.write(data) <= 0) throw IOException("Cannot write radio artifact")
            channel.force(true)
        }
        if (!path.toFile().setReadable(true, false)) throw IOException("Cannot make radio artifact readable")
    }

    private fun writeNew(path: Path, bytes: ByteArray, stageDir: Path = path.parent) {
        // Never publish an incomplete VINTF fragment; the temporary file is outside its scan directory.
        val stage = Files.createTempFile(stageDir, ".aemu-radio-", ".tmp")
        try {
            writeBytes(stage, bytes)
            Files.move(stage, path) // No REPLACE_EXISTING: preserve a name created concurrently.
        } finally { Files.deleteIfExists(stage) }
    }

    private fun installJar(target: Path, bytes: ByteArray) {
        if (!Files.exists(target, NOFOLLOW_LINKS)) { writeNew(target, bytes); return }
        if (Files.readAllBytes(target).contentEquals(bytes)) return
        val stage = Files.createTempFile(target.parent, ".aemu-radio-", ".tmp")
        try {
            writeBytes(stage, bytes)
            requireRegular(target, MAX_JAR_SIZE, "radio JAR")
            if (!isOwnedJar(target)) throw IOException("Conflicting radio JAR")
            Files.move(stage, target, ATOMIC_MOVE, REPLACE_EXISTING)
        } finally { Files.deleteIfExists(stage) }
    }
}
