package app.aemu.core

import android.view.Surface

/** JNI entry for a future EMU-owned, separate Vulkan Surface.
 * The renderer must load its native library and coordinate consumer teardown
 * before calling this. It is not connected to a view or Guest yet. */
internal object VulkanSurface {
    @JvmStatic external fun nativeUpdate(surface: Surface?): Boolean
}
