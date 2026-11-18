package app.aemu.core

import app.aemu.BuildConfig
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.concurrent.thread

/** Runs only on an explicit Debug UI action in the existing :vm process. */
internal object HostVulkanProbe {
    private val running = AtomicBoolean(false)
    private external fun nativeRun(): String

    fun start(onResult: (String) -> Unit): Boolean {
        if (!BuildConfig.DEBUG || !running.compareAndSet(false, true)) return false
        try {
            thread(name = "host-vulkan-probe") {
                try {
                    val result = try {
                        System.loadLibrary("vulkanhostprobe")
                        nativeRun()
                    } catch (e: LinkageError) {
                        "Vulkan probe library unavailable: $e"
                    }
                    onResult(result)
                } finally {
                    running.set(false)
                }
            }
        } catch (e: Exception) {
            running.set(false)
            throw e
        }
        return true
    }
}
