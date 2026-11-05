package app.aemu.ui

import app.aemu.core.GuestVm

/** A prompt for an unusually long attempt, not proof that the guest has failed. */
internal fun bootUnconfirmed(state: GuestVm.State, elapsedMs: Long): Boolean =
    (state == GuestVm.State.PREPARING || state == GuestVm.State.BOOTING) && elapsedMs >= 300_000L

internal fun bootWarningVisible(state: GuestVm.State, elapsedMs: Long, dismissedThisAttempt: Boolean): Boolean =
    bootUnconfirmed(state, elapsedMs) && !dismissedThisAttempt

