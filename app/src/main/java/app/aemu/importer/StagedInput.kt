package app.aemu.importer

import java.io.File
import java.io.IOException
import java.io.InputStream
import java.nio.file.Files

/** A private per-import snapshot, removed on success, cancellation and failure. */
internal object StagedInput {
    fun <T> use(parent: File, input: InputStream, cancelled: () -> Boolean, maxBytes: Long = Long.MAX_VALUE, block: (File) -> T): T {
        if (!parent.isDirectory && !parent.mkdirs()) throw IOException("cannot create staging directory")
        val dir = Files.createTempDirectory(parent.toPath(), "firmware-").toFile()
        var failure: Throwable? = null
        try {
            val file = File(dir, "input")
            file.outputStream().buffered(1024 * 1024).use { out ->
                val buffer = ByteArray(1024 * 1024)
                var written = 0L
                while (true) {
                    if (cancelled()) throw ImportCancelledException()
                    val count = input.read(buffer)
                    if (count < 0) break
                    if (count > 0) {
                        if (count.toLong() > maxBytes - written) throw IOException("Firmware size exceeds target")
                        out.write(buffer, 0, count)
                        written += count
                    }
                }
            }
            if (cancelled()) throw ImportCancelledException()
            return block(file)
        } catch (t: Throwable) {
            failure = t
            throw t
        } finally {
            if (!dir.deleteRecursively()) {
                val cleanup = IOException("Cannot remove incomplete firmware staging")
                if (failure != null) failure.addSuppressed(cleanup) else throw cleanup
            }
        }
    }
}
