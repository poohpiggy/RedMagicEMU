package app.aemu.core

import java.io.File
import java.io.IOException
import java.nio.ByteBuffer
import java.nio.channels.FileChannel
import java.nio.file.FileAlreadyExistsException
import java.nio.file.Files
import java.nio.file.LinkOption.NOFOLLOW_LINKS
import java.nio.file.Path
import java.nio.file.StandardOpenOption.TRUNCATE_EXISTING
import java.nio.file.StandardOpenOption.WRITE
import android.os.ParcelFileDescriptor
import android.system.ErrnoException
import android.system.Os
import android.system.OsConstants

internal enum class VibrationDeclarationStage {
    DIRECTORY, CHECK, REMOVE, CREATE_TEMP, WRITE, READABLE, PUBLISH, CLEANUP,
}

/** Carries a fixed diagnostic label only; the original exception remains available as the cause. */
internal class VibrationDeclarationStageException(
    val stage: VibrationDeclarationStage, cause: Exception,
) : IOException("vibration declaration stage: ${stage.name.lowercase()}", cause)

internal inline fun <T> vibrationDeclarationStage(stage: VibrationDeclarationStage, action: () -> T): T = try {
    action()
} catch (failure: IOException) {
    throw VibrationDeclarationStageException(stage, failure)
} catch (failure: SecurityException) {
    throw VibrationDeclarationStageException(stage, failure)
}

/**
 * Prepares only a VINTF declaration in a stopped target runtime copy.
 * This is not called during normal startup: a declaration alone does not prove
 * that the original service is isolated, started, or registered.
 */
internal object TargetVibrationDeclaration {
    private const val NAME = "aemu-redmagic-vibrator.xml"
    private const val ORIGINAL = "vendor.qti.hardware.vibrator.service.xml"
    private val content = """<manifest version="9.0" type="device">
    <hal format="aidl">
        <name>android.hardware.vibrator</name>
        <version>2</version>
        <fqname>IVibrator/default</fqname>
    </hal>
</manifest>
""".toByteArray(Charsets.UTF_8)

    /** True means only that our declaration exists with the expected contents. */
    fun prepare(root: File, api: Int, model: String, abi: String, engine: Engine, enabled: Boolean): Boolean {
        return prepareWithPublisher(root, api, model, abi, engine, enabled, ::publishNoReplace)
    }

    internal fun prepareWithPublisher(
        root: File, api: Int, model: String, abi: String, engine: Engine, enabled: Boolean,
        publish: (Path, Path) -> Unit,
    ): Boolean {
        if (api != 36 || model != "NX809J" || abi != "arm64-v8a" || engine != Engine.N64) return false
        var dir = root.toPath()
        vibrationDeclarationStage(VibrationDeclarationStage.DIRECTORY) {
            if (!Files.isDirectory(dir, NOFOLLOW_LINKS)) throw IOException("Unsafe vibration runtime root")
            for (part in listOf("vendor", "etc", "vintf", "manifest")) {
                dir = dir.resolve(part)
                if (!Files.exists(dir, NOFOLLOW_LINKS)) {
                    if (!enabled) return false
                    try { Files.createDirectory(dir) } catch (_: FileAlreadyExistsException) { }
                }
                if (!Files.isDirectory(dir, NOFOLLOW_LINKS)) throw IOException("Unsafe vibration declaration directory: $part")
            }
        }
        val target = dir.resolve(NAME)
        if (!enabled) {
            vibrationDeclarationStage(VibrationDeclarationStage.REMOVE) {
                if (Files.exists(target, NOFOLLOW_LINKS) && isOurs(target)) {
                    Files.delete(target)
                }
            }
            return false
        }
        // Do not introduce a duplicate alongside the active original fragment.
        vibrationDeclarationStage(VibrationDeclarationStage.CHECK) {
            if (Files.exists(dir.resolve(ORIGINAL), NOFOLLOW_LINKS)) throw IOException("Original vibration declaration is active")
            if (Files.exists(target, NOFOLLOW_LINKS)) { requireSame(target); return true }
        }
        val stage = vibrationDeclarationStage(VibrationDeclarationStage.CREATE_TEMP) {
            Files.createTempFile(dir, ".rme-vibration-vintf-", ".tmp")
        }
        var prior: Throwable? = null
        try {
            vibrationDeclarationStage(VibrationDeclarationStage.WRITE) {
                FileChannel.open(stage, WRITE, TRUNCATE_EXISTING, NOFOLLOW_LINKS).use { channel ->
                    val data = ByteBuffer.wrap(content)
                    while (data.hasRemaining()) if (channel.write(data) <= 0) throw IOException("Cannot write vibration declaration")
                    channel.force(true)
                }
            }
            vibrationDeclarationStage(VibrationDeclarationStage.READABLE) {
                if (!stage.toFile().setReadable(true, false)) throw IOException("Cannot make vibration declaration readable")
            }
            vibrationDeclarationStage(VibrationDeclarationStage.PUBLISH) {
                try { publish(stage, target) }
                catch (_: FileAlreadyExistsException) { requireSame(target) }
            }
        } catch (failure: Throwable) {
            prior = failure
            throw failure
        } finally {
            try {
                vibrationDeclarationStage(VibrationDeclarationStage.CLEANUP) { Files.deleteIfExists(stage) }
            } catch (cleanup: Throwable) {
                if (prior != null) prior.addSuppressed(cleanup) else throw cleanup
            }
        }
        return true
    }

    private fun publishNoReplace(stage: Path, target: Path) {
        if (!VibrationBinderExperimentalNative.available) throw IOException("Vibration publication unavailable")
        val dir = try {
            // The native publisher verifies the descriptor is a directory with fstat.
            Os.open(stage.parent.toString(), OsConstants.O_RDONLY or
                OsConstants.O_NOFOLLOW or OsConstants.O_CLOEXEC, 0)
        } catch (failure: ErrnoException) {
            throw IOException("Cannot open vibration declaration directory", failure)
        }
        var prior: Throwable? = null
        try {
            ParcelFileDescriptor.dup(dir).use { descriptor ->
                val result = VibrationBinderExperimentalNative.nativePublishNoReplace(
                    descriptor.fd, stage.fileName.toString(), target.fileName.toString(),
                )
                when (result) {
                    0 -> Unit
                    -OsConstants.EEXIST -> throw FileAlreadyExistsException(target.fileName.toString())
                    else -> throw IOException("Vibration declaration publication failed (errno ${-result})")
                }
            }
        } catch (failure: Throwable) {
            prior = failure
            throw failure
        } finally {
            try { Os.close(dir) } catch (failure: ErrnoException) {
                val closeFailure = IOException("Cannot close vibration declaration directory", failure)
                if (prior != null) prior.addSuppressed(closeFailure) else throw closeFailure
            }
        }
    }

    private fun requireSame(path: Path) {
        if (!isOurs(path)) throw IOException("Conflicting vibration declaration")
    }

    private fun isOurs(path: Path): Boolean = Files.isRegularFile(path, NOFOLLOW_LINKS) &&
        Files.size(path) == content.size.toLong() && Files.readAllBytes(path).contentEquals(content)
}
