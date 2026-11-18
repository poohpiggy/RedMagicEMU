package app.aemu.core

import app.aemu.BuildConfig
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.concurrent.thread

/** Only runs after an explicit Debug UI action, inside the existing :vm process. */
internal object HostOpenClProbe {
    private val running = AtomicBoolean(false)
    private external fun nativeRun(): String

    fun start(onResult: (String) -> Unit): Boolean {
        if (!BuildConfig.DEBUG || !running.compareAndSet(false, true)) return false
        try {
            thread(name = "host-opencl-probe") {
                try {
                    val result = try {
                        System.loadLibrary("openclhostprobe")
                        nativeRun()
                    } catch (e: LinkageError) {
                        "OpenCL probe library unavailable: $e"
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
