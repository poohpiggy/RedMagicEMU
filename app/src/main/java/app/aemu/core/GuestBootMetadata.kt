package app.aemu.core

/** Update only boot statistics on the latest persisted instance metadata. */
internal object GuestBootMetadata {
    fun apply(current: GuestImage, bootMs: Long): GuestImage =
        current.copy(lastBootMs = bootMs, bootCount = current.bootCount + 1)
}
