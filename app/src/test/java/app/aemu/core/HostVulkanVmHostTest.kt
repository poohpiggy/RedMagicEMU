package app.aemu.core

import org.junit.Assert.*
import org.junit.Test
import java.io.File
import java.util.Collections
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference
import kotlin.concurrent.thread

class HostVulkanVmHostTest {
    private fun child(mode: String = "normal"): ProcessBuilder {
        val java = File(System.getProperty("java.home"), "bin/java").absolutePath
        val paths = listOf(HostVulkanServiceTestChild::class.java, kotlin.jvm.internal.Intrinsics::class.java)
            .joinToString(File.pathSeparator) { File(it.protectionDomain.codeSource.location.toURI()).path }
        return ProcessBuilder(java, "-cp", paths, HostVulkanServiceTestChild::class.java.name, mode)
    }

    @Test fun selectedOnlyForTargetGpuInstance() {
        assertTrue(HostVulkanVmHost.selected("NX809J", 36, "arm64-v8a", true, true))
        assertFalse(HostVulkanVmHost.selected("NX809J", 36, "arm64-v8a", true, false))
        assertFalse(HostVulkanVmHost.selected("NX809J", 36, "arm64-v8a", false, true))
        assertFalse(HostVulkanVmHost.selected("NX809J", 35, "arm64-v8a", true, true))
        assertFalse(HostVulkanVmHost.selected("other", 36, "arm64-v8a", true, true))
        assertFalse(HostVulkanVmHost.selected("NX809J", 36, "armeabi-v7a", true, true))
    }

    @Test fun startsNativeLibraryAndReportsOnlyHostReadiness() {
        val command = AtomicReference<List<String>>()
        val process = AtomicReference<Process>()
        val service = HostVulkanService { builder ->
            command.set(builder.command().toList()); child().start().also { process.set(it) }
        }
        val log = Collections.synchronizedList(mutableListOf<String>())
        val host = HostVulkanVmHost(service, { log.add(it) }) { 123 }
        try {
            host.start(File("/example/native/libaemuvkhost.so"))
            assertEquals("libaemuvkhost.so", File(command.get().first()).name)
            assertEquals(listOf("--parent-pid", "123"), command.get().drop(1))
            assertTrue(service.isRunning())
            assertTrue(log.any { it.contains("host", true) && it.contains("Guest") })
        } finally { host.cancel(); host.stop() }
        assertFalse(process.get().isAlive)
    }

    @Test fun startsPackagedRendererWithInstanceDisplay() {
        val command = AtomicReference<List<String>>()
        val libPath = AtomicReference<String>()
        val service = HostVulkanService { builder ->
            command.set(builder.command().toList())
            libPath.set(builder.environment()["LD_LIBRARY_PATH"])
            child().start()
        }
        val log = Collections.synchronizedList(mutableListOf<String>())
        val host = HostVulkanVmHost(service, { log.add(it) }) { 123 }
        val nativeDir = File("native").absoluteFile
        try {
            host.startRenderer(nativeDir, 720, 1600, 240)
            assertEquals(File(nativeDir, "libaemuvkrenderer.so").absolutePath, command.get().first())
            assertEquals(listOf("--parent-pid", "123", "720", "1600", "240"), command.get().drop(1))
            assertEquals(nativeDir.absolutePath, libPath.get())
            assertTrue(service.isRunning())
            assertTrue(log.any { it.contains("Vulkan renderer ready") })
        } finally { host.cancel(); host.stop() }
    }

    @Test fun failedStartIsLoggedAndDoesNotThrow() {
        val service = HostVulkanService { throw java.io.IOException("synthetic") }
        val log = mutableListOf<String>()
        val host = HostVulkanVmHost(service, { log.add(it) }) { 123 }
        host.start(File("/example/native/libaemuvkhost.so"))
        assertFalse(service.isRunning())
        assertTrue(log.any { it.contains("failed", true) })
        host.cancel(); host.stop()
    }

    @Test fun cancelledStartCannotLeaveLateProcess() {
        val entered = CountDownLatch(1)
        val release = CountDownLatch(1)
        val process = AtomicReference<Process>()
        val service = HostVulkanService { entered.countDown(); release.await(); child().start().also { process.set(it) } }
        val host = HostVulkanVmHost(service, {}) { 123 }
        val worker = thread { host.start(File("/example/native/libaemuvkhost.so")) }
        assertTrue(entered.await(3, TimeUnit.SECONDS))
        host.cancel()
        release.countDown()
        worker.join(10000)
        assertFalse(worker.isAlive)
        host.stop()
        assertFalse(process.get().isAlive)
    }

    @Test fun stopFailureRetainsVmLifecycleForRetry() {
        val process = AtomicReference<Process>()
        val service = HostVulkanService { child("ignore").start().also { process.set(it) } }
        val host = HostVulkanVmHost(service, {}) { 123 }
        val vm = VmLifecycle()
        assertTrue(vm.begin())
        vm.endBoot()
        try {
            host.start(File("/example/native/libaemuvkhost.so"))
            assertTrue(service.isRunning())
            var failed = false
            try { vm.stop(onRequest = { host.cancel() }) { host.stop() } }
            catch (_: IllegalStateException) { failed = true }
            assertTrue("forced or unconfirmed stop must propagate", failed)
            assertFalse("failed cleanup must keep the VM active", vm.begin())
            vm.stop(onRequest = { host.cancel() }) { host.stop() }
            assertTrue("successful retry permits a new VM session", vm.begin())
            vm.endBoot()
        } finally {
            runCatching { vm.stop(onRequest = { host.cancel() }) { host.stop() } }
            process.get()?.destroyForcibly()
        }
    }
}
