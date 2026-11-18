package app.aemu.core

import android.app.Service
import android.content.Intent
import android.os.Binder
import android.os.IBinder
import android.os.Process
import android.view.Surface

/** Private Surface transport process. The renderer is not connected yet. */
class VulkanSurfaceService : Service() {
    private val binder = object : IVulkanSurfaceService.Stub() {
        override fun status(): Int = VULKAN_SURFACE_NOT_READY

        override fun attachSurface(surface: Surface?, generation: Long): Int =
            rejectUnavailableVulkanSurface(Binder.getCallingPid(), Process.myPid()) { surface?.release() }

        override fun detachSurface(generation: Long): Int = VULKAN_SURFACE_NOT_READY
    }

    override fun onBind(intent: Intent?): IBinder = binder
}
