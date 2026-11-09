package app.aemu.core

/** Independent, currently unwired experimental Binder bridge library. */
internal object VibrationBinderExperimentalNative {
    val available: Boolean = try {
        System.loadLibrary("aemuvibration")
        true
    } catch (_: UnsatisfiedLinkError) {
        false
    }

    /** The native side duplicates and verifies both descriptors; caller retains ownership. */
    @JvmStatic external fun nativeOpen(socketFd: Int, pidfd: Int, pid: Int, uid: Int, header: ByteArray): Long

    /** [status, event token, code, durationMs]; -1 error, 1 stopped, 2 event, 3 registered.
     * Registration reports only the addService reply, not service ownership or device support. */
    @JvmStatic external fun nativeNext(handle: Long): LongArray?
    @JvmStatic external fun nativeComplete(handle: Long, token: Long, success: Boolean): Int
    @JvmStatic external fun nativeStop(handle: Long): Int

    /** Atomic no-overwrite publication in a pinned directory; 0 or negative errno. */
    @JvmStatic external fun nativePublishNoReplace(directoryFd: Int, stagedName: String, targetName: String): Int

    /** Stops the native worker, waits for in-flight native calls, then releases the handle. */
    @JvmStatic external fun nativeDestroy(handle: Long): Int
}
