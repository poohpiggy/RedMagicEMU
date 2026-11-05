package app.aemu.ui

internal enum class SingleSystemState { LOADING, EMPTY, IMPORTING, READY, CONFLICT }

internal fun singleSystemState(count: Int, importing: Boolean, loaded: Boolean = true, completed: Boolean = false): SingleSystemState = when {
    importing -> SingleSystemState.IMPORTING
    !loaded || (completed && count == 0) -> SingleSystemState.LOADING
    count == 0 -> SingleSystemState.EMPTY
    count == 1 -> SingleSystemState.READY
    else -> SingleSystemState.CONFLICT
}
