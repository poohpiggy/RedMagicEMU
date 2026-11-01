package app.aemu.core

/** Хост-помощник libaemuhost.so (native/hostjni). На Android 8–9 fdsan нет — библиотека не грузится, это не ошибка. */
object HostNative {
    private val ok: Boolean = try { System.loadLibrary("aemuhost"); true } catch (_: Throwable) { false }

    @JvmStatic private external fun relaxFdsan()
    @JvmStatic private external fun limitStack()

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
