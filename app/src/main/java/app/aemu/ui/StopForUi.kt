package app.aemu.ui

internal fun stopForUi(stop: () -> Unit, success: () -> Unit, failure: (Exception) -> Unit) {
    try { stop() } catch (e: Exception) { failure(e); return }
    success()
}

internal fun rejectDifferentRunningVm(currentId: String, requestedId: String, state: app.aemu.core.GuestVm.State): Boolean = currentId != requestedId && state != app.aemu.core.GuestVm.State.STOPPED

internal fun allowPowerOffRetry(stopPending: Boolean): Boolean = !stopPending
