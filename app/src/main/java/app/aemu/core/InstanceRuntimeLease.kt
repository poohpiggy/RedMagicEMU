package app.aemu.core

import android.system.Os
import android.system.OsConstants
import java.io.Closeable
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.nio.channels.FileChannel
import java.nio.channels.FileLock
import java.nio.channels.OverlappingFileLockException
import java.nio.file.Files
import java.nio.file.LinkOption.NOFOLLOW_LINKS
import java.nio.file.Path
import java.nio.file.StandardOpenOption.CREATE
import java.nio.file.StandardOpenOption.WRITE
import java.util.concurrent.atomic.AtomicBoolean

/** Experimental, cooperative lease. Not wired to VM startup or an isolation guarantee. */
internal class InstanceRuntimeLease private constructor(
    private val handle: Handle,
    private val lock: FileLock,
    private val pathIdentity: () -> Any?,
    private val key: Any,
) : Closeable {
    private val closed = AtomicBoolean(false)

    // A valid file lock alone does not prove Guest child processes stopped after owner death.
    fun isValid(): Boolean = !closed.get() && lock.isValid && handle.channel.isOpen && runCatching {
        handle.descriptorIdentity() == key && pathIdentity() == key
    }.getOrDefault(false)

    override fun close() {
        if (!closed.compareAndSet(false, true)) return
        try { lock.release() } finally { handle.close() }
    }

    private class Handle(val channel: FileChannel, val descriptorIdentity: () -> Any?, val close: () -> Unit) {
        fun close() = close.invoke()
    }

    private data class Inode(val device: Long, val inode: Long)

    companion object {
        const val FILE_NAME = ".redmagic-runtime.lock"

        /** Uses the opened descriptor's inode, not a pre-open or post-open path guess. */
        fun acquire(instance: File, id: String): InstanceRuntimeLease = acquireImpl(instance, id, { path ->
            val fd = Os.open(path.toString(), OsConstants.O_CREAT or OsConstants.O_RDWR or
                OsConstants.O_NOFOLLOW or OsConstants.O_CLOEXEC, 384 /* 0600 */)
            val stream = try { FileOutputStream(fd) } catch (t: Throwable) { Os.close(fd); throw t }
            Handle(stream.channel, {
                val stat = Os.fstat(fd)
                if (OsConstants.S_ISREG(stat.st_mode)) Inode(stat.st_dev, stat.st_ino) else null
            }) { stream.close() }
        }, { path ->
            val stat = Os.lstat(path.toString())
            if (OsConstants.S_ISREG(stat.st_mode)) Inode(stat.st_dev, stat.st_ino) else null
        })

        /** JVM-only seam: tests locking, not Android fd identity. Never used for a real VM. */
        internal fun acquireForTests(instance: File, id: String, identity: (Path) -> Any?): InstanceRuntimeLease =
            acquireForTests(instance, id, identity, identity)

        internal fun acquireForTests(instance: File, id: String, fdIdentity: (Path) -> Any?,
                                     pathIdentity: (Path) -> Any?): InstanceRuntimeLease =
            acquireImpl(instance, id, { path ->
                val channel = FileChannel.open(path, CREATE, WRITE, NOFOLLOW_LINKS)
                Handle(channel, { fdIdentity(path) }) { channel.close() }
            }, pathIdentity)

        private fun acquireImpl(instance: File, id: String, open: (Path) -> Handle,
                                identity: (Path) -> Any?): InstanceRuntimeLease {
            val base = instance.absoluteFile.parentFile ?: throw IOException("Invalid instance parent")
            if (instance.name != id || !Files.isDirectory(base.toPath(), NOFOLLOW_LINKS) ||
                !InstancePathSafety.safeInstance(base, id)) throw IOException("Invalid instance identity")
            val path = File(instance, FILE_NAME).toPath()
            val handle = try { open(path) } catch (e: Exception) { throw IOException("Cannot open runtime lock", e) }
            try {
                val key = handle.descriptorIdentity() ?: throw IOException("Cannot verify descriptor identity")
                if (identity(path) != key) throw IOException("Runtime lock inode mismatch")
                val lock = try { handle.channel.tryLock() } catch (_: OverlappingFileLockException) { null }
                    ?: throw IOException("Instance is already in use")
                val lease = InstanceRuntimeLease(handle, lock, { identity(path) }, key)
                if (!lease.isValid()) { lease.close(); throw IOException("Runtime lock identity changed") }
                return lease
            } catch (t: Throwable) {
                try { handle.close() } catch (close: Exception) { t.addSuppressed(close) }
                if (t is IOException) throw t
                throw IOException("Cannot acquire runtime lease", t)
            }
        }
    }
}
