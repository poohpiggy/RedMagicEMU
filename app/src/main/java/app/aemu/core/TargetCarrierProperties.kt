package app.aemu.core

import java.io.File
import java.io.IOException
import java.nio.ByteBuffer
import java.nio.charset.CodingErrorAction
import java.nio.file.Files
import java.nio.file.LinkOption
import java.nio.file.StandardOpenOption

/** Read only the target variant record and the confirmed project property set from a private guest. */
internal object TargetCarrierProperties {
    private const val SIZE = 524288L
    private const val START = 4096L
    private const val RECORDS = 192
    private const val FIELD = 32
    private const val GENERIC = "GEN_CN"
    private val allowed = setOf(
        "persist.vendor.custom.variant.id", "ro.oem.key1", "ro.vendor.build.def_theme_name", "ro.vendor.product.ztename",
        "ro.vendor.feature.zte_feature_theme_resource", "ro.vendor.feature.zte_feature_launcher_resource",
        "ro.config.message_notification_sound1", "ro.config.message_notification_sound2", "ro.config.ringtone",
        "ro.vendor.config.ringtone_2", "ro.config.notification_sound", "ro.config.message",
        "ro.config.message_2", "ro.config.alarm_alert",
    )
    sealed class Result {
        data object MissingImage : Result()
        data object NotTarget : Result()
        data class Loaded(val properties: Map<String, String>, val fallback: Boolean, val variant: String) : Result()
        data class Invalid(val reason: String) : Result()
    }
    private class InvalidInput(message: String) : IOException(message)

    /** All path components must be inside the private root and none may be a link. */
    private fun checked(root: File, parts: List<String>): java.nio.file.Path? {
        var p = root.toPath()
        if (Files.isSymbolicLink(p) || !Files.isDirectory(p, LinkOption.NOFOLLOW_LINKS)) throw InvalidInput("invalid guest root")
        for ((index, part) in parts.withIndex()) {
            p = p.resolve(part)
            if (Files.isSymbolicLink(p)) throw InvalidInput("linked input path")
            if (!Files.exists(p, LinkOption.NOFOLLOW_LINKS)) return null
            if (index < parts.lastIndex && !Files.isDirectory(p, LinkOption.NOFOLLOW_LINKS)) throw InvalidInput("invalid input directory")
        }
        return p
    }
    private fun field(bytes: ByteArray, offset: Int): String? {
        val end = (offset until offset + FIELD).firstOrNull { bytes[it] == 0.toByte() } ?: return null
        if ((offset until end).any { (bytes[it].toInt() and 255) !in 32..126 }) return null
        return String(bytes, offset, end - offset, Charsets.US_ASCII)
    }
    private fun variant(path: java.nio.file.Path): String? {
        if (!Files.isRegularFile(path, LinkOption.NOFOLLOW_LINKS) || Files.size(path) != SIZE) throw InvalidInput("invalid ztecfg image")
        val b = ByteBuffer.allocate(RECORDS * FIELD * 4)
        Files.newByteChannel(path, StandardOpenOption.READ, LinkOption.NOFOLLOW_LINKS).use { input ->
            input.position(START)
            while (b.hasRemaining()) if (input.read(b) < 0) throw InvalidInput("truncated ztecfg image")
        }
        val data = b.array()
        for (i in 0 until RECORDS) {
            val at = i * FIELD * 4
            if (field(data, at) != "this-is-cfg-head" || field(data, at + FIELD) != "zte_variantflag" ||
                field(data, at + FIELD * 2) != "cfg-signature-is-here") continue
            val value = field(data, at + FIELD * 3) ?: throw InvalidInput("invalid variant record")
            if (!value.matches(Regex("[A-Za-z0-9_]{1,31}"))) throw InvalidInput("invalid variant name")
            return value
        }
        return null
    }
    private fun projectDirectory(root: File, name: String): java.nio.file.Path? {
        val path = checked(root, listOf("system", "carrier", "projects", name)) ?: return null
        if (!Files.isDirectory(path, LinkOption.NOFOLLOW_LINKS)) throw InvalidInput("invalid project directory")
        return path
    }
    private fun properties(root: File, name: String): Map<String, String> {
        val path = checked(root, listOf("system", "carrier", "projects", name, "prop_fixed.txt"))
            ?: throw InvalidInput("missing project properties")
        if (!Files.isRegularFile(path, LinkOption.NOFOLLOW_LINKS) || Files.size(path) > 65536L) throw InvalidInput("invalid project properties")
        val bytes = Files.newInputStream(path, StandardOpenOption.READ, LinkOption.NOFOLLOW_LINKS).use { input ->
            val out = java.io.ByteArrayOutputStream()
            val buffer = ByteArray(4096)
            while (true) {
                val n = input.read(buffer)
                if (n < 0) break
                if (n == 0) continue
                if (n > 65536 - out.size()) throw InvalidInput("oversized project properties")
                out.write(buffer, 0, n)
            }
            out.toByteArray()
        }
        val text = Charsets.UTF_8.newDecoder().onMalformedInput(CodingErrorAction.REPORT)
            .onUnmappableCharacter(CodingErrorAction.REPORT).decode(ByteBuffer.wrap(bytes)).toString()
        val parsed = LinkedHashMap<String, String>()
        for (raw in text.lineSequence()) {
            val line = raw.trim()
            if (line.isEmpty() || line.startsWith("#")) continue
            val pos = line.indexOf('=')
            if (pos <= 0) throw InvalidInput("malformed project properties")
            val key = line.substring(0, pos).trim()
            if (key !in allowed) throw InvalidInput("unsupported project property")
            val value = line.substring(pos + 1).trim()
            if (value.any { it.code < 32 || it.code == 127 } || value.toByteArray(Charsets.UTF_8).size >= 92)
                throw InvalidInput("invalid project value")
            parsed[key] = value
        }
        return parsed
    }
    fun load(root: File, api: Int, model: String, abi: String): Result {
        if (api != 36 || model != "NX809J" || abi != "arm64-v8a") return Result.NotTarget
        return try {
            val path = checked(root, listOf("dev", "block", "by-name", "ztecfg")) ?: return Result.MissingImage
            val flag = variant(path)
            var fallback = flag == null
            var selected = flag ?: GENERIC
            if (projectDirectory(root, selected) == null) { selected = GENERIC; fallback = true }
            if (projectDirectory(root, selected) == null) throw InvalidInput("missing default project")
            Result.Loaded(properties(root, selected), fallback, selected)
        } catch (e: InvalidInput) { Result.Invalid(e.message ?: "invalid input") }
        catch (_: Exception) { Result.Invalid("unable to read project configuration") }
    }
    fun requireVariantMatches(expected: String, observed: String?) {
        check(observed == expected) { "carrier: selected project and effective variant differ; framework start aborted" }
    }
}

/** Project properties are loaded after ordinary boot defaults but before saved and runtime values. */
internal fun applyBootPropertyLayers(area: PropArea, defaults: Map<String, String>, carrier: Map<String, String>,
                                     persistent: Map<String, String>, overrides: Map<String, String>): Int {
    var count = 0
    fun saved() { for ((k, v) in persistent) if (area.put(k, v)) count++ }
    fun ordinary() { for ((k, v) in defaults) if (area.get(k) == null) area.put(k, v) }
    if (carrier.isEmpty()) {
        saved(); ordinary()
    } else {
        ordinary()
        for ((k, v) in carrier) if (!area.put(k, v)) throw IOException("Cannot apply target project property")
        saved()
    }
    for ((k, v) in overrides) area.put(k, v)
    return count
}
