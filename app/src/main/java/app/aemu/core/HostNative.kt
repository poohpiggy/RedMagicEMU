package app.aemu.core

/** Хост-помощник libaemuhost.so (native/hostjni). На Android 8–9 fdsan нет — библиотека не грузится, это не ошибка. */
object HostNative {
    private val ok: Boolean = try { System.loadLibrary("aemuhost"); true } catch (_: Throwable) { false }

    @JvmStatic private external fun relaxFdsan()
    @JvmStatic private external fun limitStack()
    @JvmStatic external fun pidfdOpen(pid: Int): Int
    @JvmStatic external fun pidfdSignal(fd: Int, signal: Int): Int
    @JvmStatic external fun pidfdClose(fd: Int)
    /** Nonblocking; 0 means no packet, -1 invalid/closed, 1..3 authenticated ACK. */
    @JvmStatic external fun radioReadAck(socketFd: Int, pidfd: Int, pid: Int, uid: Int, epoch: Long, sequence: Long): Int
    /** Dedicated vibration connection: -1 reject, 0 empty, 1 cancel, otherwise milliseconds+1. */
    @JvmStatic external fun vibrationRead(socketFd: Int, pidfd: Int, pid: Int, uid: Int, epoch: Long, sequence: Long): Int

    /** Закрытый GL-мост делает freopen() поверх fd 2 — без этого fdsan роняет процесс :vm. */
    /** RLIMIT_STACK 8 MB for guest processes (bionic 6.0 derives the main thread's stack bounds from it). */
    fun limitStackSafe() { if (ok) try { limitStack() } catch (_: Throwable) {} }

    fun relaxFdsanSafe() { if (ok) try { relaxFdsan() } catch (_: Throwable) {} }

    /** AF_UNIX datagram sockets (WifiStub): bind → fd or -errno; recv blocks, from[0] = sender path; close wakes recv. */
    @JvmStatic external fun dgramBind(path: String): Int
    @JvmStatic external fun dgramRecv(fd: Int, buf: ByteArray, from: Array<String?>): Int
    @JvmStatic external fun dgramSend(fd: Int, data: ByteArray, to: String): Int
    @JvmStatic external fun dgramClose(fd: Int)
    val available: Boolean get() = ok
}
