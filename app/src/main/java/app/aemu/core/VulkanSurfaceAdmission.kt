package app.aemu.core

internal val VULKAN_SURFACE_NOT_READY: Int = IVulkanSurfaceService.NOT_READY

/** Release only the receiver's Binder-unparcelled reference, never a local caller's object. */
internal fun rejectUnavailableVulkanSurface(callerPid: Int, servicePid: Int, release: () -> Unit): Int {
    if (callerPid != servicePid) release()
    return VULKAN_SURFACE_NOT_READY
}
