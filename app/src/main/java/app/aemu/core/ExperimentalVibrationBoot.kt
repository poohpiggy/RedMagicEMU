package app.aemu.core

/**
 * Prepares the offline-only experiment. The caller must remove the declaration
 * again when the service cannot start. A prepared declaration is not evidence
 * that the HAL is safe or usable.
 */
internal fun prepareExperimentalVibration(
    enabled: Boolean,
    adapterReady: Boolean,
    declaration: (Boolean) -> Boolean,
): Boolean {
    if (!enabled || !adapterReady) {
        declaration(false)
        return false
    }
    return try {
        if (declaration(true)) true else {
            declaration(false)
            false
        }
    } catch (failure: Throwable) {
        try { declaration(false) } catch (cleanup: Throwable) { failure.addSuppressed(cleanup) }
        throw failure
    }
}
