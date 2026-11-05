package app.aemu.ui

/** A cancellation request survives until the background importer is ready to receive it. */
internal class ImportCancellation {
    private var requested = false
    private var listener: (() -> Unit)? = null

    @Synchronized fun cancel() {
        requested = true
        listener?.invoke()
    }

    /** Returns false when cancellation was already requested. */
    @Synchronized fun attach(onCancel: () -> Unit): Boolean {
        if (requested) {
            onCancel()
            return false
        }
        listener = onCancel
        return true
    }
}

internal fun isUserCancellation(error: Throwable): Boolean =
    error is app.aemu.importer.ImportCancelledException && error.suppressed.isEmpty()
