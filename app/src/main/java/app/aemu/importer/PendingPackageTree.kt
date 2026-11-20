package app.aemu.importer

import java.io.Closeable
import java.io.File
import java.io.IOException
import java.nio.ByteBuffer
import java.nio.channels.FileChannel
import java.nio.file.Path
import java.nio.file.StandardOpenOption.READ
import java.util.concurrent.ExecutionException
import java.util.concurrent.FutureTask

/** Builds only the unpublished tree. Its result is available after the archive passes verification. */
internal class PendingPackageTree(
    private val root: File,
    private val cancelled: () -> Boolean,
    private val extract: (RandomSource, File, File, () -> Boolean) -> List<BootImage.CpioEntry> = TargetPackageTree::extract,
) : Closeable {
    private val monitor = Object()
    @Volatile private var available = 0L
    @Volatile private var stopped = false
    private var verified = false
    private var task: FutureTask<List<BootImage.CpioEntry>>? = null
    private var worker: Thread? = null

    fun prefixAvailable(file: Path, size: Long, ramdisk: File, end: Long) {
        synchronized(monitor) {
            available = end
            monitor.notifyAll()
        }
        if (worker != null) return
        val work = FutureTask {
            FileChannel.open(file, READ).use { channel ->
                val source = StrictChannelSource(channel, size)
                val prefix = object : RandomSource {
                    override val size: Long = source.size
                    override fun read(pos: Long, dst: ByteBuffer) {
                        if (pos < 0 || pos > size || dst.remaining().toLong() > size - pos)
                            throw IOException("Firmware image read outside bounds")
                        waitForPrefix(pos + dst.remaining())
                        source.read(pos, dst)
                    }
                }
                extract(prefix, ramdisk, root, ::shouldStop)
            }
        }
        task = work
        worker = Thread(work, "firmware-file-tree").also { it.start() }
    }

    fun markVerified() { verified = true }

    fun awaitVerified(): List<BootImage.CpioEntry> {
        if (!verified) throw IOException("Firmware archive has not been verified")
        val work = task ?: throw IOException("Missing decoded super.img")
        return try {
            work.get()
        } catch (e: ExecutionException) {
            throw e.cause ?: e
        } catch (_: InterruptedException) {
            Thread.currentThread().interrupt()
            throw ImportCancelledException()
        }
    }

    private fun shouldStop(): Boolean = stopped || cancelled()

    private fun waitForPrefix(end: Long) {
        if (available < end && !shouldStop()) {
            try {
                synchronized(monitor) {
                    while (available < end && !shouldStop()) monitor.wait()
                }
            } catch (_: InterruptedException) {
                Thread.currentThread().interrupt()
                throw ImportCancelledException()
            }
        }
        if (shouldStop()) throw ImportCancelledException()
    }

    override fun close() {
        synchronized(monitor) {
            stopped = true
            monitor.notifyAll()
        }
        val thread = worker ?: return
        var interrupted = false
        try {
            // The owner's archive cleanup must wait until the worker releases its files.
            while (true) {
                try {
                    thread.join()
                    break
                } catch (_: InterruptedException) {
                    interrupted = true
                }
            }
        } finally {
            if (interrupted) Thread.currentThread().interrupt()
        }
    }
}
