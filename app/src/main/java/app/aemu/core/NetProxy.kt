package app.aemu.core

import android.content.Context

/**
 * Сетевые помощники гостя. Сеть у гостя работает напрямую (qemu передаёт сокеты в ядро телефона),
 * а прокси нужны для старых браузеров: HTTP-прокси и TLS-мост с современными шифрами.
 */
class NetProxy(private val ctx: Context, private val paths: VmPaths, private val api: Int, private val is32: Boolean, private val log: (String) -> Unit) {
    private var tls: TlsBridge? = null

    fun start(): GuestRunner.NetConfig {
        // every 32-bit firmware (kernel 23 redirects): 1.5-2.2 speak only TLS 1.0, which today's servers and the
        // phone's TLS refuse; later ones get TLS 1.2/1.3 and today's roots from the phone for apps that ask for less.
        // 64-bit kernels have no redirect yet.
        if (!is32 || api < 3) return GuestRunner.NetConfig()
        val t = TlsBridge(ctx, paths, log)
        val port = t.start()
        tls = t
        return GuestRunner.NetConfig(https443 = port)
    }

    fun stop() {
        tls?.stop()
        tls = null
    }
}
