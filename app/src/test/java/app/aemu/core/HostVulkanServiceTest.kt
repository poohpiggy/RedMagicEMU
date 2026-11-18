package app.aemu.core

import org.junit.Assert.*
import org.junit.Test
import java.io.File
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.atomic.AtomicReference
import kotlin.concurrent.thread

/** Fake subprocess: no Android, Vulkan, sockets, device files, or Guest. */
object HostVulkanServiceTestChild {
    @JvmStatic fun main(args: Array<String>) {
        when (args.firstOrNull()) {
            "exit" -> kotlin.system.exitProcess(13)
            "device-fail" -> { System.err.println("RME_VK_HOST_DEVICE_FAILED vk=-4"); System.err.flush(); kotlin.system.exitProcess(4) }
            "ready-exit" -> { System.out.println("RME_VK_HOST_READY"); System.out.flush(); kotlin.system.exitProcess(9) }
            "ignore" -> { System.out.println("RME_VK_HOST_READY"); System.out.flush(); Thread.sleep(30000) }
            "wrong" -> { System.out.println("NOT_READY"); System.out.flush(); Thread.sleep(30000) }
            "no-ready" -> Thread.sleep(30000)
            "delayed" -> { Thread.sleep(800); System.out.println("RME_VK_HOST_READY"); System.out.flush(); System.`in`.read() }
            "normal", "noisy" -> {
                System.out.println("RME_VK_HOST_READY"); System.out.flush()
                if (args[0] == "noisy") {
                    repeat(96) { System.err.write(ByteArray(2048) { 'e'.code.toByte() }) }
                    System.err.flush()
                    repeat(96) { System.out.write(ByteArray(2048) { 'o'.code.toByte() }) }
                    System.out.flush()
                }
                if (System.`in`.read() != 'Q'.code) kotlin.system.exitProcess(7)
            }
            else -> kotlin.system.exitProcess(3)
        }
    }
}

class HostVulkanServiceTest {
    private class InterruptedWaitProcess(private val wrapped: Process) : Process() {
        private var first = true
        override fun getOutputStream() = wrapped.outputStream
        override fun getInputStream() = wrapped.inputStream
        override fun getErrorStream() = wrapped.errorStream
        override fun waitFor(): Int {
            if (first) { first = false; throw InterruptedException("synthetic") }
            return wrapped.waitFor()
        }
        override fun waitFor(timeout: Long, unit: TimeUnit) = wrapped.waitFor(timeout, unit)
        override fun exitValue() = wrapped.exitValue()
        override fun destroy() = wrapped.destroy()
        override fun destroyForcibly(): Process { wrapped.destroyForcibly(); return this }
        override fun isAlive() = wrapped.isAlive
    }

    private class FailedTimedWaitProcess(private val wrapped: Process) : Process() {
        private var first = true
        override fun getOutputStream() = wrapped.outputStream
        override fun getInputStream() = wrapped.inputStream
        override fun getErrorStream() = wrapped.errorStream
        override fun waitFor() = wrapped.waitFor()
        override fun waitFor(timeout: Long, unit: TimeUnit): Boolean {
            if (first) { first = false; throw IllegalStateException("synthetic") }
            return wrapped.waitFor(timeout, unit)
        }
        override fun exitValue() = wrapped.exitValue()
        override fun destroy() = wrapped.destroy()
        override fun destroyForcibly(): Process { wrapped.destroyForcibly(); return this }
        override fun isAlive() = wrapped.isAlive
    }

    private class FailedDestroyProcess(private val wrapped: Process) : Process() {
        private var first = true
        override fun getOutputStream() = wrapped.outputStream
        override fun getInputStream() = wrapped.inputStream
        override fun getErrorStream() = wrapped.errorStream
        override fun waitFor() = wrapped.waitFor()
        override fun waitFor(timeout: Long, unit: TimeUnit) = wrapped.waitFor(timeout, unit)
        override fun exitValue() = wrapped.exitValue()
        override fun destroy() {
            if (first) { first = false; throw IllegalStateException("synthetic") }
            wrapped.destroy()
        }
        override fun destroyForcibly(): Process { wrapped.destroyForcibly(); return this }
        override fun isAlive() = wrapped.isAlive
    }

    private fun child(mode: String): ProcessBuilder {
        val java = File(System.getProperty("java.home"), "bin/java").absolutePath
        val paths = listOf(
            HostVulkanServiceTestChild::class.java.protectionDomain.codeSource.location.toURI(),
            kotlin.jvm.internal.Intrinsics::class.java.protectionDomain.codeSource.location.toURI(),
        ).joinToString(File.pathSeparator) { File(it).path }
        return ProcessBuilder(java, "-cp", paths, HostVulkanServiceTestChild::class.java.name, mode)
    }

    @Test fun startsOnlyAfterReadyAndStopsOnQuit() {
        val service = HostVulkanService()
        try {
            val result = service.start(child("normal"), 5000)
            assertTrue(result.message, result.ok)
            assertTrue(service.isRunning())
            assertTrue(service.stop(5000).ok)
            assertFalse(service.isRunning())
        } finally { service.stop(5000) }
    }

    @Test fun duplicateStartDoesNotSpawnAnotherChild() {
        val calls = AtomicInteger()
        val service = HostVulkanService { builder -> calls.incrementAndGet(); builder.start() }
        try {
            assertTrue(service.start(child("normal"), 5000).ok)
            assertFalse(service.start(child("normal"), 1000).ok)
            assertEquals(1, calls.get())
        } finally { service.stop(5000) }
    }

    @Test fun redirectedStreamsAreRejectedBeforeLaunching() {
        val calls = AtomicInteger()
        val service = HostVulkanService { builder -> calls.incrementAndGet(); builder.start() }
        assertFalse(service.start(child("normal").redirectErrorStream(true), 1000).ok)
        assertEquals(0, calls.get())
    }

    @Test fun failedLauncherReturnsPromptlyAndPreservesError() {
        val service = HostVulkanService { throw java.io.IOException("synthetic") }
        val before = System.nanoTime()
        val result = service.start(child("normal"), 5000)
        assertFalse(result.ok)
        assertTrue(result.message.contains("IOException"))
        assertTrue(TimeUnit.NANOSECONDS.toMillis(System.nanoTime() - before) < 2000)
        assertNotNull(service.lastFailure())
    }

    @Test fun interruptedReaperStillConfirmsExitAndAllowsRestart() {
        val calls = AtomicInteger()
        val service = HostVulkanService { builder ->
            val process = builder.start()
            if (calls.getAndIncrement() == 0) InterruptedWaitProcess(process) else process
        }
        try {
            service.start(child("normal"), 5000)
            assertTrue("interrupted reaper did not confirm exit", service.stop(3000).ok)
            val next = service.start(child("normal"), 5000)
            assertTrue(next.message, next.ok)
        } finally { service.stop(5000) }
    }

    @Test fun stopThreadCreationFailureCanBeRetried() {
        val calls = AtomicInteger()
        val service = HostVulkanService(spawnStop = { block ->
            if (calls.getAndIncrement() == 0) throw IllegalStateException("synthetic")
            thread(isDaemon = true, block = block)
            Unit
        })
        try {
            assertTrue(service.start(child("normal"), 5000).ok)
            assertFalse(service.stop(500).ok)
            assertTrue(service.lastFailure()!!.contains("IllegalStateException"))
            assertTrue("retry failed", service.stop(5000).ok)
            assertTrue(calls.get() >= 2)
        } finally { service.stop(5000) }
    }

    @Test fun stopWorkerFailureCanBeRetriedWithoutPretendingExit() {
        val created = AtomicReference<Process>()
        val service = HostVulkanService { builder ->
            builder.start().also { created.set(it) }.let { FailedTimedWaitProcess(it) }
        }
        try {
            assertTrue(service.start(child("ignore"), 5000).ok)
            assertFalse(service.stop(500).ok)
            assertTrue("live child treated as stopped", created.get().isAlive)
            assertTrue(service.lastFailure()!!.contains("IllegalStateException"))
            assertFalse("forced termination must be reported", service.stop(5000).ok)
            assertFalse("retry failed to reap child", created.get().isAlive)
        } finally { service.stop(5000) }
    }

    @Test fun failedDestroyCanBeRetriedWithoutPretendingExit() {
        val created = AtomicReference<Process>()
        val service = HostVulkanService { builder ->
            builder.start().also { created.set(it) }.let { FailedDestroyProcess(it) }
        }
        try {
            assertTrue(service.start(child("ignore"), 5000).ok)
            assertFalse(service.stop(5000).ok)
            assertTrue("live child treated as stopped", created.get().isAlive)
            assertTrue(service.lastFailure()!!.contains("IllegalStateException"))
            assertFalse("forced termination must be reported", service.stop(5000).ok)
            assertFalse("retry failed to reap child", created.get().isAlive)
        } finally { service.stop(5000) }
    }

    @Test fun earlyExitAndWrongReadyAreFailures() {
        for (mode in listOf("exit", "wrong")) {
            val service = HostVulkanService()
            try {
                val result = service.start(child(mode), 5000)
                assertFalse("$mode was accepted", result.ok)
                assertFalse(service.isRunning())
                assertNotNull(service.lastFailure())
            } finally { service.stop(5000) }
        }
    }

    @Test fun nativeFailureDetailsAreRetainedWithinBound() {
        val service = HostVulkanService()
        try {
            val result = service.start(child("device-fail"), 5000)
            assertFalse(result.ok)
            assertTrue("native error lost: ${result.message}", result.message.contains("RME_VK_HOST_DEVICE_FAILED vk=-4"))
            assertTrue(service.lastFailure()!!.contains("RME_VK_HOST_DEVICE_FAILED vk=-4"))
        } finally { service.stop(5000) }
    }

    @Test fun readyThenExitDoesNotRemainRunning() {
        val service = HostVulkanService()
        try {
            service.start(child("ready-exit"), 5000)
            val until = System.nanoTime() + TimeUnit.SECONDS.toNanos(5)
            while ((service.isRunning() || service.lastFailure() == null) && System.nanoTime() < until) Thread.yield()
            assertFalse(service.isRunning())
            assertNotNull(service.lastFailure())
        } finally { service.stop(5000) }
    }

    @Test fun forcedTerminationIsConfirmedAndReported() {
        val created = AtomicReference<Process>()
        val service = HostVulkanService { builder -> builder.start().also { created.set(it) } }
        try {
            assertTrue(service.start(child("ignore"), 5000).ok)
            val result = service.stop(5000)
            assertFalse("forced stop must be distinguishable", result.ok)
            assertTrue(result.message.contains("forced"))
            assertFalse("child survived forced termination", created.get().isAlive)
            assertNotNull(service.lastFailure())
        } finally { service.stop(5000) }
    }

    @Test fun timeoutCancelsProcessAndAllowsLaterRestart() {
        val service = HostVulkanService()
        try {
            assertFalse(service.start(child("no-ready"), 300).ok)
            service.stop(5000)
            assertTrue(service.start(child("normal"), 5000).ok)
            assertTrue(service.stop(5000).ok)
        } finally { service.stop(5000) }
    }

    @Test fun stopDuringStartCannotPublishLateReady() {
        val entered = CountDownLatch(1)
        val service = HostVulkanService { builder -> entered.countDown(); builder.start() }
        var result: HostVulkanService.Result? = null
        val launcher = thread { result = service.start(child("delayed"), 5000) }
        assertTrue(entered.await(3, TimeUnit.SECONDS))
        try {
            service.stop(5000)
            launcher.join(5000)
            assertFalse("start did not return", launcher.isAlive)
            assertFalse("late readiness was accepted", result!!.ok)
            assertFalse(service.isRunning())
        } finally { service.stop(5000); launcher.join(5000) }
    }

    @Test fun largeStdoutAndStderrAreDrained() {
        val service = HostVulkanService()
        try {
            assertTrue(service.start(child("noisy"), 5000).ok)
            Thread.sleep(300)
            assertTrue(service.stop(5000).ok)
        } finally { service.stop(5000) }
    }

    @Test fun stalledFactoryDoesNotBlockStopAndLateChildIsReaped() {
        val entered = CountDownLatch(1)
        val release = CountDownLatch(1)
        val created = AtomicReference<Process>()
        val service = HostVulkanService { builder -> entered.countDown(); release.await(); builder.start().also { created.set(it) } }
        val launcher = thread { service.start(child("normal"), 5000) }
        assertTrue(entered.await(3, TimeUnit.SECONDS))
        try {
            val before = System.nanoTime()
            assertFalse(service.stop(200).ok)
            assertTrue(TimeUnit.NANOSECONDS.toMillis(System.nanoTime() - before) < 2000)
            assertFalse("overlapping launch accepted", service.start(child("normal"), 200).ok)
        } finally { release.countDown() }
        launcher.join(5000)
        service.stop(5000)
        assertFalse(launcher.isAlive)
        assertFalse(service.isRunning())
        assertNotNull(created.get())
        assertFalse("late child survived cancellation", created.get().isAlive)
    }
}
