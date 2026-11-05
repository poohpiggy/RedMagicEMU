package app.aemu.ui

import app.aemu.core.GuestVm

/** VM-owned observer: it must remain registered when the page is destroyed. */
internal class BootHistoryRecorder(private val history: BootHistory, private val id: String) {
    @Volatile var error: Exception? = null
        private set
    fun onState(state: GuestVm.State) {
        try {
            when (state) {
                GuestVm.State.FAILED -> history.failed(id)
                GuestVm.State.STOPPED -> history.stopped(id)
                else -> return
            }
            error = null
        } catch (e: Exception) {
            error = e
            android.util.Log.e("aemu-boot", "Could not persist boot state", e)
        }
    }
}

internal fun showHistoryWriteErrorOnAttach(recorder: BootHistoryRecorder?): Boolean = recorder?.error != null
