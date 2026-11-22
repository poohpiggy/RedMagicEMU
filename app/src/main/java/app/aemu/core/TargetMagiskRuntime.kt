package app.aemu.core

import android.os.Process
import android.os.SystemClock
import android.system.ErrnoException
import android.system.Os
import android.system.OsConstants
import android.system.StructStat
import app.aemu.core.TargetMagiskPayload.MANAGER_PACKAGE
import app.aemu.core.TargetMagiskPayload.VERSION
import app.aemu.core.TargetMagiskPayload.VERSION_CODE
import java.io.Closeable
import java.io.File
import java.nio.file.Files
import java.nio.file.LinkOption.NOFOLLOW_LINKS
import java.nio.file.Paths
import java.nio.file.StandardCopyOption.ATOMIC_MOVE
import java.security.MessageDigest

/** Owns only the boot files staged for this instance. The Guest's policy database persists. */
internal class TargetMagiskRuntime(private val paths: VmPaths) : Closeable {
    private val directory = File(paths.root, "debug_ramdisk")
    private val internal = File(directory, ".magisk")
    private val binary = File(directory, "magisk")
    private val su = File(directory, "su")
    private val tools = File(internal, "rme-bin")
    private val marker = File(paths.bin, "magisk-runtime")

    fun prepare(interpreter: File) {
        close()
        check(HostNative.available) { "Magisk requires pidfd process tracking" }
        val handle = HostNative.pidfdOpen(Process.myPid())
        check(handle >= 0) { "Magisk requires pidfd process tracking" }
        HostNative.pidfdClose(handle)
        ensureDirectory(directory)
        check(listOf(binary, su, internal).none { Files.exists(it.toPath(), NOFOLLOW_LINKS) }) {
            "Guest already has Magisk boot files"
        }
        check(File(internal, "device/socket").absolutePath.toByteArray().size < 108) {
            "Guest Magisk socket path is too long"
        }
        val original = paths.ctx.assets.open("rme-magisk/magisk").use { it.readBytes() }
        check(sha256(original) == assetDigest("magisk")) {
            "Bundled Magisk ARM64 digest mismatch"
        }
        val patched = TargetMagiskPayload.withInterpreter(original, interpreter.absolutePath)
        check(marker.createNewFile()) { "Magisk runtime is already prepared" }
        var staged: File? = null
        try {
            check(File(internal, "device").mkdirs()) { "Cannot create the Magisk runtime directory" }
            val preinit = TargetMagiskPayload.preparePreinit(paths.root)
            Os.chmod(preinit.absolutePath, 0x1c0)
            Files.createSymbolicLink(File(internal, "preinit").toPath(), internal.toPath().relativize(preinit.toPath()))
            check(tools.mkdir()) { "Cannot create the Guest Magisk tool directory" }
            for (name in listOf("busybox", "magiskboot", "magiskinit", "magiskpolicy",
                "util_functions.sh", "boot_patch.sh", "app_functions.sh")) {
                val file = File(tools, name)
                paths.ctx.assets.open("rme-magisk/tools/$name").use { input ->
                    file.outputStream().use { output -> input.copyTo(output) }
                }
                if (!name.endsWith(".sh")) {
                    check(file.setExecutable(true, true)) { "Cannot mark Guest Magisk tool executable: $name" }
                }
            }
            staged = File.createTempFile(".rme-magisk-", ".elf", internal)
            staged.writeBytes(patched)
            check(staged.setExecutable(true, true)) { "Cannot mark Guest Magisk executable" }
            Files.move(staged.toPath(), binary.toPath(), ATOMIC_MOVE)
            Files.createSymbolicLink(su.toPath(), Paths.get("magisk"))
        } catch (failure: Exception) {
            runCatching { close() }.onFailure(failure::addSuppressed)
            throw failure
        } finally {
            staged?.delete()
        }
    }

    fun start(runner: GuestRunner) {
        val environment = runner.runBounded(
            listOf("/system/bin/sh", "-c",
                "export MAGISKTMP=/debug_ramdisk; . /debug_ramdisk/.magisk/rme-bin/app_functions.sh; " +
                    "env_check $VERSION $VERSION_CODE"), 12_000)
        check(environment is BoundedGuestCommand.Result.Completed && environment.exitCode == 0) {
            "Guest Magisk environment check failed: ${describe(environment)}"
        }
        val launch = command(runner, "--daemon </dev/null >/debug_ramdisk/.magisk/start.log 2>&1")
        check(launch is BoundedGuestCommand.Result.Completed && launch.exitCode == 0) {
            val details = runCatching {
                File(internal, "start.log").bufferedReader().use { reader ->
                    val buffer = CharArray(512)
                    val count = reader.read(buffer).coerceAtLeast(0)
                    String(buffer, 0, count).trim()
                }
            }.getOrDefault("")
            ("Guest Magisk startup failed: ${describe(launch)} $details").trim()
        }
        val version = command(runner, "-V")
        check(version is BoundedGuestCommand.Result.Completed && version.exitCode == 0 &&
            version.output.trim() == VERSION_CODE) { "Guest Magisk $VERSION did not respond: ${describe(version)}" }
    }

    fun installManager(runner: GuestRunner, shellPrefix: String): String {
        val expectedDigest = assetDigest("manager.apk")
        val (code, output) = runner.run(
            listOf("/system/bin/sh", "-c", shellPrefix + "pm path $MANAGER_PACKAGE"), 20_000)
        val path = output.lineSequence().map(String::trim)
            .singleOrNull { it.startsWith("package:/data/app/") && it.endsWith("/base.apk") }
            ?.removePrefix("package:/")
        if (code == 0 && path != null) {
            val installed = File(paths.root, path).canonicalFile
            val appDir = File(paths.root.canonicalFile, "data/app")
            if (installed.toPath().startsWith(appDir.toPath()) && installed.isFile &&
                sha256(installed.readBytes()) == expectedDigest) {
                return "Guest Magisk Manager $VERSION is already installed"
            }
        }
        val apk = File.createTempFile("magisk-manager-", ".apk", paths.bin)
        try {
            paths.ctx.assets.open("rme-magisk/manager.apk").use { input ->
                apk.outputStream().use { output -> input.copyTo(output) }
            }
            check(sha256(apk.readBytes()) == expectedDigest) { "Bundled Guest Magisk Manager digest mismatch" }
            val result = runner.installStream(shellPrefix + "pm install -r -S ${apk.length()}", apk, apk.length(), 120_000)
            check(result.success) { result.output.trim().takeLast(320) }
            return "Guest Magisk Manager $VERSION installed"
        } finally {
            apk.delete()
        }
    }

    override fun close() {
        if (!Files.exists(marker.toPath(), NOFOLLOW_LINKS)) return
        check(Files.isRegularFile(marker.toPath(), NOFOLLOW_LINKS) && marker.length() == 0L)
        ensureDirectory(directory)
        if (Files.exists(binary.toPath(), NOFOLLOW_LINKS)) {
            check(Files.isRegularFile(binary.toPath(), NOFOLLOW_LINKS))
            val executable = Os.stat(binary.absolutePath)
            File("/proc").list().orEmpty().mapNotNull(String::toIntOrNull).filter { it > 0 }.forEach { pid ->
                if (sameExecutable(pid, executable)) terminate(pid, executable)
            }
        }
        if (Files.exists(su.toPath(), NOFOLLOW_LINKS)) {
            check(Files.isSymbolicLink(su.toPath()) && Files.readSymbolicLink(su.toPath()) == Paths.get("magisk"))
            Files.delete(su.toPath())
        }
        Files.deleteIfExists(binary.toPath())
        if (Files.exists(internal.toPath(), NOFOLLOW_LINKS)) {
            check(Files.isDirectory(internal.toPath(), NOFOLLOW_LINKS))
            Files.walk(internal.toPath()).use { entries ->
                entries.sorted(Comparator.reverseOrder()).forEach { Files.delete(it) }
            }
        }
        Files.delete(marker.toPath())
    }

    private fun terminate(pid: Int, executable: StructStat) {
        val fd = HostNative.pidfdOpen(pid)
        if (fd == -OsConstants.ESRCH) return
        check(fd >= 0) { "Cannot hold the Guest Magisk process identity" }
        try {
            if (!sameExecutable(pid, executable)) return
            val result = HostNative.pidfdSignal(fd, OsConstants.SIGKILL)
            check(result == 0 || result == -OsConstants.ESRCH) { "Cannot stop the Guest Magisk process" }
            val deadline = SystemClock.elapsedRealtime() + 2_000
            while (sameExecutable(pid, executable) && SystemClock.elapsedRealtime() < deadline) {
                SystemClock.sleep(20)
            }
            check(!sameExecutable(pid, executable)) { "Guest Magisk process did not exit" }
        } finally {
            HostNative.pidfdClose(fd)
        }
    }

    private fun sameExecutable(pid: Int, executable: StructStat): Boolean = try {
        val process = Os.stat("/proc/$pid/exe")
        process.st_dev == executable.st_dev && process.st_ino == executable.st_ino
    } catch (failure: ErrnoException) {
        if (failure.errno !in setOf(OsConstants.ENOENT, OsConstants.ESRCH, OsConstants.EACCES, OsConstants.EPERM))
            throw failure
        false
    }

    private fun ensureDirectory(dir: File) {
        if (!Files.exists(dir.toPath(), NOFOLLOW_LINKS)) check(dir.mkdir())
        check(Files.isDirectory(dir.toPath(), NOFOLLOW_LINKS) && dir.canonicalFile.parentFile == paths.root.canonicalFile) {
            "Unexpected Guest Magisk runtime directory"
        }
    }

    private fun command(runner: GuestRunner, flag: String) =
        runner.runBounded(listOf("/system/bin/sh", "-c", "exec /debug_ramdisk/magisk $flag"), 12_000)

    private fun assetDigest(name: String): String =
        paths.ctx.assets.open("rme-magisk/$name.sha256").bufferedReader().use { it.readText().trim() }

    private fun sha256(bytes: ByteArray): String =
        MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") { "%02x".format(it) }

    private fun describe(result: BoundedGuestCommand.Result): String = when (result) {
        is BoundedGuestCommand.Result.Completed -> result.output.trim().takeLast(300).ifBlank { "exit ${result.exitCode}" }
        is BoundedGuestCommand.Result.Uncertain -> result.reason.name
    }

    companion object {
        fun applies(img: GuestImage): Boolean = img.api == 36 && img.model == "NX809J" &&
            img.abi == "arm64-v8a" && img.effective().engine == Engine.N64
    }
}
