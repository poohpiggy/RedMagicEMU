package app.aemu.importer

import java.io.IOException

/** Linearizes cancellation with making an instance visible. */
internal class PublicationGate {
    @Volatile private var requested = false
    private var published = false

    val cancelled: Boolean get() = requested

    @Synchronized fun setCancelled(value: Boolean) {
        if (!published) requested = value
    }

    @Synchronized fun publish(action: () -> Unit) {
        if (requested) throw ImportCancelledException()
        action()
        published = true
    }
}
