package app.aemu.core
/** Remove only the dead instance the watchdog observed, never its replacement. */
internal fun <T : Any> removeDeadDnsVerifier(processes: MutableMap<String,T>, observed: T, alive: (T) -> Boolean) {
    synchronized(processes) {
        if (processes["redmagic-wifi-dns"] === observed && !alive(observed)) processes.remove("redmagic-wifi-dns")
    }
}
/** Retrying is safe only after all resources from the previous session are closed. */
internal fun finishDnsRetirement(cleanup: () -> Unit, after: () -> Unit) {
    cleanup()
    after()
}
internal fun captureDnsStopFailure(close: () -> Unit): Throwable? = try { close(); null } catch (failure: Throwable) { failure }

/** Tracks cleanup through completion and retains failures instead of allowing a duplicate session. */
internal class TargetDnsRetirementGuard {
    private var pending = 0
    private var failure: Throwable? = null
    @Synchronized fun begin() { pending++ }
    @Synchronized fun complete() { check(pending > 0); pending-- }
    @Synchronized fun failed(error: Throwable) {
        check(pending > 0); pending--
        if (failure == null) failure = error else if (failure !== error) failure!!.addSuppressed(error)
    }
    @Synchronized fun canStart(): Boolean = pending == 0 && failure == null
    @Synchronized fun requireComplete() {
        failure?.let { throw it }
        check(pending == 0) { "Wi-Fi DNS cleanup remains in progress" }
    }
}
/** Only the verifier process uses this channel, and it may not start without one. */
internal fun <T : Any> dnsChannelForSpawn(name: String, channel: T?): T? {
    if (name != "redmagic-wifi-dns") return null
    return checkNotNull(channel) { "Wi-Fi DNS verifier channel unavailable" }
}

/** A skipped start or a stale entry must not be reported as a new verifier. */
internal fun <T : Any> dnsVerifierWasStarted(previous: T?, current: T?): Boolean =
    current != null && current !== previous

/** One startup at a time; a concurrent attempt must retry instead of creating another channel. */
internal class TargetDnsStartGate {
    enum class Decision { START, BUSY, ACTIVE }
    private var starting = false
    @Synchronized fun begin(hasCurrent: Boolean): Decision {
        if (starting) return Decision.BUSY
        if (hasCurrent) return Decision.ACTIVE
        starting = true
        return Decision.START
    }
    @Synchronized fun end() { starting = false }
}
internal class TargetDnsRestartPolicy {
    private val lock=java.lang.Object()
    private var failures=0
    private var stopped=false
    private var pending=false
    fun afterExit(runtimeMs: Long): Long? = synchronized(lock) {
        require(runtimeMs >= 0)
        if (stopped || pending) return null
        pending=true
        if (runtimeMs >= 60_000) failures=0
        val delay=when (failures) { 0 -> 500L; 1 -> 1000L; 2 -> 2000L; 3 -> 4000L; 4 -> 8000L; 5 -> 16000L; else -> 30000L }
        if (failures < 6) failures++
        delay
    }
    fun awaitRetry(delayMs: Long): Boolean = synchronized(lock) {
        require(delayMs >= 0)
        val deadline=System.nanoTime()+java.util.concurrent.TimeUnit.MILLISECONDS.toNanos(delayMs)
        while (!stopped) {
            val remaining=deadline-System.nanoTime()
            if (remaining <= 0) { pending=false; return true }
            java.util.concurrent.TimeUnit.NANOSECONDS.timedWait(lock,remaining)
        }
        false
    }
    fun cancelRetry() = synchronized(lock) { pending=false }
    fun stop() = synchronized(lock) { stopped=true; lock.notifyAll() }
}
