package app.aemu.core

import android.os.ParcelFileDescriptor
import android.system.Os
import android.system.OsConstants
import java.io.File
import java.io.IOException

internal object SafeFramebufferPath {
    fun components(id: String, recovery: Boolean): List<String> {
        require(ImageId.valid(id)) { "invalid instance identity" }
        return listOf("images", id, if (recovery) "recovery" else "root", "dev", "graphics", "fb0")
    }
}

/** Opens only an instance framebuffer, without following any guest-controlled path component. */
internal object SafeFramebuffer {
    class Opened(val stream: ParcelFileDescriptor.AutoCloseInputStream, val device: Long, val inode: Long, val size: Long) : AutoCloseable {
        override fun close() = stream.close()
    }

    fun open(filesDir: File, id: String, recovery: Boolean): Opened {
        val parts = SafeFramebufferPath.components(id, recovery)
        val dirFlags = OsConstants.O_RDONLY or OsConstants.O_NOFOLLOW or OsConstants.O_NONBLOCK or OsConstants.O_CLOEXEC
        fun duplicate(path: String, flags: Int): ParcelFileDescriptor {
            val fd = Os.open(path, flags, 0)
            return try { ParcelFileDescriptor.dup(fd) } finally { Os.close(fd) }
        }
        fun directory(path: String): ParcelFileDescriptor {
            val fd = duplicate(path, dirFlags)
            try {
                if (!OsConstants.S_ISDIR(Os.fstat(fd.fileDescriptor).st_mode)) throw IOException("framebuffer parent is not a directory")
                return fd
            } catch (e: Throwable) { fd.close(); throw e }
        }
        // filesDir is the OS-provided application directory, outside the guest-controlled instance tree.
        var parent = directory(filesDir.absolutePath)
        try {
            for (part in parts.dropLast(1)) {
                val next = directory("/proc/self/fd/${parent.fd}/$part")
                val previous = parent
                parent = next
                previous.close()
            }
            val file = duplicate("/proc/self/fd/${parent.fd}/${parts.last()}",
                OsConstants.O_RDONLY or OsConstants.O_NOFOLLOW or OsConstants.O_NONBLOCK or OsConstants.O_CLOEXEC)
            try {
                val stat = Os.fstat(file.fileDescriptor)
                if (!OsConstants.S_ISREG(stat.st_mode)) throw IOException("framebuffer is not a regular file")
                return Opened(ParcelFileDescriptor.AutoCloseInputStream(file), stat.st_dev, stat.st_ino, stat.st_size)
            } catch (e: Throwable) { file.close(); throw e }
        } finally { parent.close() }
    }
}
