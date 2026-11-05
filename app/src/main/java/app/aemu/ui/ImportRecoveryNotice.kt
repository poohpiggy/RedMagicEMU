package app.aemu.ui

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

data class ImportRecoveryNoticeState(val token: String? = null, val error: String? = null)

/** Serializes recovery, display state and acknowledgement without hiding import results. */
internal class ImportRecoveryNotice(
    private val recover: () -> String?,
    private val acknowledge: (String) -> String?,
) {
    private val mutable = MutableStateFlow(ImportRecoveryNoticeState())
    val state: StateFlow<ImportRecoveryNoticeState> = mutable

    @Synchronized fun recover() {
        try { mutable.value = ImportRecoveryNoticeState(token = recover.invoke()) }
        catch (e: Exception) { mutable.value = mutable.value.copy(error = e.message ?: e.javaClass.simpleName) }
    }

    @Synchronized fun acknowledge(displayed: String) {
        try { mutable.value = ImportRecoveryNoticeState(token = acknowledge.invoke(displayed)) }
        catch (e: Exception) { mutable.value = mutable.value.copy(error = e.message ?: e.javaClass.simpleName) }
    }
}
