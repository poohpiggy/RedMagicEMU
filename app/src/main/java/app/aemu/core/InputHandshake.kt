package app.aemu.core

internal fun readInputDevice(setTimeout: (Int) -> Unit, read: () -> Int): Int {
    val dev = try {
        setTimeout(500)
        runCatching { read() }.getOrDefault(-1)
    } finally {
        setTimeout(0)
    }
    return if (dev == 0 || dev == 1) dev else -1
}
