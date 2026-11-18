package app.aemu.core

import java.io.File

/** Coordinates the per-VM renderer with VM cancellation. */
internal class HostVulkanVmHost(
    private val service: HostVulkanService,
    private val log: (String) -> Unit,
    private val parentPid: () -> Int,
) {
    @Volatile private var cancelled = false

    companion object {
        const val RENDERER_EXECUTABLE = "libaemuvkrenderer.so"

        fun selected(model: String, api: Int, abi: String, native64: Boolean, gpu: Boolean): Boolean =
            model == "NX809J" && api == 36 && abi == "arm64-v8a" && native64 && gpu
    }

    fun begin() { cancelled = false }
    fun cancel() { cancelled = true }

    /** Called from a lifecycle worker, never while holding the VM lifecycle lock. */
    fun start(executable: File) {
        start(
            ProcessBuilder(executable.absolutePath, "--parent-pid", parentPid().toString()),
            "Vulkan host service", 5_000, 250,
        )
    }

    fun startRenderer(nativeDir: File, width: Int, height: Int, density: Int) {
        val executable = File(nativeDir, RENDERER_EXECUTABLE)
        val builder = ProcessBuilder(
            executable.absolutePath, "--parent-pid", parentPid().toString(),
            width.toString(), height.toString(), density.toString(),
        ).apply { environment()["LD_LIBRARY_PATH"] = nativeDir.absolutePath }
        start(builder, "Vulkan renderer", 15_000, 2_000)
    }

    fun startGuestRenderer(nativeDir: File, root: File, runner: File, width: Int, height: Int, density: Int): Boolean {
        val builder = ProcessBuilder(
            File(nativeDir, RENDERER_EXECUTABLE).absolutePath, "--parent-pid", parentPid().toString(),
            width.toString(), height.toString(), density.toString(),
            "--guest-root", root.absolutePath, runner.absolutePath,
        ).apply {
            environment()["LD_LIBRARY_PATH"] = nativeDir.absolutePath
            environment()["RME_VULKAN_SHARED_MEMORY"] = "cached"
        }
        return start(builder, "Guest GPU renderer", 15_000, 2_000, servesGuest = true)
    }

    private fun start(builder: ProcessBuilder, name: String, startupMs: Long, shutdownGraceMs: Long,
                      servesGuest: Boolean = false): Boolean {
        if (cancelled) return false
        val result = try {
            service.start(builder, startupMs, shutdownGraceMs)
        } catch (t: Throwable) {
            if (!cancelled) log("$name failed: ${t.javaClass.simpleName}")
            runCatching { stop() }.onFailure { log("Vulkan host cleanup unconfirmed: ${it.message}") }
            return false
        }
        if (cancelled) {
            runCatching { stop() }.onFailure { log("Vulkan host cleanup unconfirmed: ${it.message}") }
            return false
        }
        if (result.ok && service.isRunning()) {
            log(if (servesGuest) "$name ready" else "$name ready (host only, Guest Vulkan not connected)")
            return true
        }
        log("$name failed: ${if (result.ok) "host exited after readiness" else result.message}")
        runCatching { stop() }.onFailure { log("Vulkan host cleanup unconfirmed: ${it.message}") }
        return false
    }

    /** Propagate unconfirmed cleanup so VmLifecycle can retain the session for retry. */
    fun stop() {
        val result = service.stop(5_000)
        if (!result.ok) throw IllegalStateException(result.message)
    }
}
