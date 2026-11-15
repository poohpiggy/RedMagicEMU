package app.aemu.core
import java.io.Closeable
internal class WifiDnsAvailabilityNotifier(private val retryMs: Long, private val send: (Boolean)->Boolean): Closeable {
    private val lock=java.lang.Object()
    private var closed=false
    private var wanted: Boolean?=null
    private var version=0L
    private var delivered=-1L
    private var failure: Throwable?=null
    private val worker: Thread
    init {
        require(retryMs in 1..60_000)
        worker=Thread({ run() },"wifi-dns-availability").apply { isDaemon=true; start() }
    }
    fun update(value: Boolean, force: Boolean = false) = synchronized(lock) {
        if (closed || (!force && wanted == value)) return@synchronized
        wanted=value; version++; lock.notifyAll()
    }
    private fun run() {
        try {
            while (true) {
                val (value, current)=synchronized(lock) {
                    while (!closed && (wanted==null || delivered==version)) lock.wait()
                    if (closed) return
                    wanted!! to version
                }
                val ok=try { send(value) } catch (_: Exception) { false }
                synchronized(lock) {
                    if (closed) return
                    if (version==current) {
                        if (ok) delivered=current else lock.wait(retryMs)
                    }
                }
            }
        } catch (error: Throwable) {
            synchronized(lock) { failure=error; closed=true; lock.notifyAll() }
        }
    }
    override fun close() {
        synchronized(lock) { closed=true; lock.notifyAll() }
        check(Thread.currentThread() !== worker) { "cannot close Wi-Fi DNS notifier on its worker" }
        worker.join(6000)
        check(!worker.isAlive) { "Wi-Fi DNS notifier did not stop" }
        failure?.let { throw IllegalStateException("Wi-Fi DNS notifier failed",it) }
    }
}
