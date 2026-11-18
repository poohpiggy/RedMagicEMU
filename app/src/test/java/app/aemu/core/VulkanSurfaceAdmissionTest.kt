package app.aemu.core

import org.junit.Assert.assertEquals
import org.junit.Test

class VulkanSurfaceAdmissionTest {
    @Test fun remoteParcelIsReleasedAndRendererReportedUnavailable() {
        var releases = 0
        val result = rejectUnavailableVulkanSurface(callerPid = 13, servicePid = 22) { releases++ }
        assertEquals(VULKAN_SURFACE_NOT_READY, result)
        assertEquals(1, releases)
    }

    @Test fun localCallerKeepsItsOwnSurface() {
        var releases = 0
        val result = rejectUnavailableVulkanSurface(callerPid = 22, servicePid = 22) { releases++ }
        assertEquals(VULKAN_SURFACE_NOT_READY, result)
        assertEquals(0, releases)
    }
}
