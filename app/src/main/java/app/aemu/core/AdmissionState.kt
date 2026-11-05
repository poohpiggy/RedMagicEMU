package app.aemu.core

/** In-memory lifecycle state; every boot must still run the verifier. */
internal class AdmissionState {
    @Volatile var admitted = false
        private set
    @Synchronized fun reset() { admitted = false }
    @Synchronized fun ifAdmitted(action: () -> Unit) { if (admitted) action() }
    fun verify(check: () -> Unit) {
        reset()
        check()
        synchronized(this) { admitted = true }
    }
}
