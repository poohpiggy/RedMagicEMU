package app.aemu.core

import java.io.File
import java.io.IOException
import java.io.InputStream
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference
import java.util.concurrent.atomic.AtomicBoolean

internal class ApkInstallGate {
    private val busy = AtomicBoolean(false)
    fun tryAcquire(): Boolean = busy.compareAndSet(false, true)
    fun release() { busy.set(false) }
}

internal data class ApkStreamResult(val success: Boolean, val output: String, val exitCode: Int? = null)

/** The destination is a unique private file, never the selected source. */
internal fun stageApk(input: InputStream, destination: File, maxBytes: Long = 2L * 1024 * 1024 * 1024): Long {
    require(maxBytes > 0)
    var count = 0L
    destination.outputStream().use { out ->
        val buffer = ByteArray(64 * 1024)
        while (true) {
            val n = input.read(buffer)
            if (n < 0) break
            if (n == 0) throw IOException("APK source returned no data")
            if (count > maxBytes - n) throw IOException("APK exceeds staging limit")
            out.write(buffer, 0, n)
            count += n
        }
    }
    if (count <= 0 || destination.length() != count) throw IOException("Empty or incomplete APK")
    return count
}

/** The caller must redirect stdin to the staged file before starting the process. */
internal fun runApkInstallStream(p: Process, staged: File, size: Long, timeoutMs: Long): ApkStreamResult {
    val result = StringBuilder()
    val readError = AtomicReference<Throwable?>(null)
    val reader = Thread {
        try {
            p.inputStream.bufferedReader().use { stream ->
                val buffer = CharArray(4096)
                while (true) {
                    val n = stream.read(buffer)
                    if (n < 0) break
                    result.append(buffer, 0, n)
                    if (result.length > 65536) result.delete(0, result.length - 65536)
                }
            }
        } catch (t: Exception) { readError.set(t) }
    }.apply { isDaemon = true; start() }
    try {
        if (size <= 0 || staged.length() != size) return ApkStreamResult(false, "Incomplete APK")
        if (!p.waitFor(timeoutMs, TimeUnit.MILLISECONDS)) return ApkStreamResult(false, "Install timed out")
        reader.join(2000)
        if (reader.isAlive || readError.get() != null) return ApkStreamResult(false, "Unable to read installation result")
        val output = result.toString()
        val exitCode = p.exitValue()
        val success = exitCode == 0 && staged.length() == size && output.lineSequence().lastOrNull { it.isNotBlank() }?.trim() == "Success"
        return ApkStreamResult(success, output, exitCode)
    } finally {
        if (p.isAlive) {
            p.destroyForcibly()
            check(p.waitFor(5, TimeUnit.SECONDS)) { "Install process did not terminate" }
        }
        if (reader.isAlive) p.inputStream.close()
    }
}
