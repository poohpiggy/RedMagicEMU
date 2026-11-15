package app.aemu.core

import java.io.File
import java.io.IOException
import java.nio.file.Files
import java.nio.file.LinkOption.NOFOLLOW_LINKS
import java.nio.file.Path

/** Preserve the target firmware's multihal config so the N64 HIDL wrapper loads sensors.default.so. */
internal object TargetSensorsConfig {
    fun prepare(root: File, api: Int, model: String, abi: String, engine: Engine): Boolean {
        if (api != 36 || model != "NX809J" || abi != "arm64-v8a" || engine != Engine.N64) return false
        val base = root.toPath()
        if (!Files.isDirectory(base, NOFOLLOW_LINKS)) throw IOException("Invalid guest root")
        var parent: Path = base
        for (part in listOf("vendor", "etc", "sensors")) {
            parent = parent.resolve(part)
            if (!Files.exists(parent, NOFOLLOW_LINKS)) return false
            if (!Files.isDirectory(parent, NOFOLLOW_LINKS)) throw IOException("Unsafe sensor configuration directory: $part")
        }
        val source = parent.resolve("hals.conf")
        if (!Files.exists(source, NOFOLLOW_LINKS)) return false
        if (!Files.isRegularFile(source, NOFOLLOW_LINKS)) throw IOException("Unsafe sensor configuration")
        val system = base.resolve("system")
        if (!Files.isDirectory(system, NOFOLLOW_LINKS)) throw IOException("Unsafe guest system directory")
        val parked = system.resolve(".aemu-parked")
        if (!Files.exists(parked, NOFOLLOW_LINKS)) Files.createDirectory(parked)
        if (!Files.isDirectory(parked, NOFOLLOW_LINKS)) throw IOException("Unsafe firmware quarantine directory")
        val backup = parked.resolve("vendor#etc#sensors#hals.conf")
        if (Files.exists(backup, NOFOLLOW_LINKS)) throw IOException("Conflicting parked sensor configuration")
        Files.move(source, backup)
        return true
    }
}
