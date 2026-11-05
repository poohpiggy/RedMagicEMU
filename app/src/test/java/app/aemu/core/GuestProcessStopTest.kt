package app.aemu.core

import org.junit.Assert.*
import org.junit.Test
import java.io.InputStream
import java.io.OutputStream
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.TimeUnit

class GuestProcessStopTest {
    private class ProcessProbe(private val exits: Boolean = true) : Process() {
        var waited = false
        override fun getOutputStream(): OutputStream = OutputStream.nullOutputStream()
        override fun getInputStream(): InputStream = InputStream.nullInputStream()
        override fun getErrorStream(): InputStream = InputStream.nullInputStream()
        override fun waitFor(): Int = 0
        override fun waitFor(timeout: Long, unit: TimeUnit): Boolean { waited = true; return exits }
        override fun exitValue(): Int = 0
        override fun destroy() {}
        override fun destroyForcibly(): Process = this
        override fun isAlive(): Boolean = !waited
    }
    @Test fun trackedProcessesAreReapedBeforeCleanupReturns() {
        val cls = Class.forName("sun.misc.Unsafe")
        val f = cls.getDeclaredField("theUnsafe").apply { isAccessible = true }
        val vm = cls.getMethod("allocateInstance", Class::class.java).invoke(f.get(null), GuestVm::class.java) as GuestVm
        fun set(name: String, value: Any) { GuestVm::class.java.getDeclaredField(name).apply { isAccessible = true }.set(vm, value) }
        val process = ProcessProbe()
        set("procs", linkedMapOf("test" to process))
        set("externalProcs", ConcurrentHashMap.newKeySet<Process>())
        try {
            GuestVm::class.java.getDeclaredMethod("killAll").apply { isAccessible = true }.invoke(vm)
        } catch (e: java.lang.reflect.InvocationTargetException) {
            // Android's Process.myPid is unavailable in local JVM tests, after tracked cleanup.
            check(e.cause is RuntimeException && e.cause?.message?.contains("not mocked") == true) { e.cause.toString() }
        }
        assertTrue("cleanup never waited for the killed process", process.waited)
    }
    @Test fun failedTerminationDoesNotDiscardTrackedProcess() {
        val cls = Class.forName("sun.misc.Unsafe")
        val f = cls.getDeclaredField("theUnsafe").apply { isAccessible = true }
        val vm = cls.getMethod("allocateInstance", Class::class.java).invoke(f.get(null), GuestVm::class.java) as GuestVm
        fun set(name: String, value: Any) { GuestVm::class.java.getDeclaredField(name).apply { isAccessible = true }.set(vm, value) }
        val process = ProcessProbe(false)
        val procs = linkedMapOf("test" to process)
        set("procs", procs)
        set("externalProcs", ConcurrentHashMap.newKeySet<Process>())
        set("lines", java.util.concurrent.CopyOnWriteArrayList<String>())
        set("listeners", java.util.concurrent.CopyOnWriteArrayList<(String) -> Unit>())
        set("admissionState", AdmissionState())
        try {
            GuestVm::class.java.getDeclaredMethod("killAll").apply { isAccessible = true }.invoke(vm)
            fail("cleanup must fail while a process is still alive")
        } catch (e: java.lang.reflect.InvocationTargetException) {
            assertTrue(e.cause is IllegalStateException)
        }
        assertSame(process, procs["test"])
    }

    @Test fun alreadyExitedProcessDoesNotFailWhenSharedDeadlineHasPassed() {
        val cls = Class.forName("sun.misc.Unsafe")
        val field = cls.getDeclaredField("theUnsafe").apply { isAccessible = true }
        val vm = cls.getMethod("allocateInstance", Class::class.java).invoke(field.get(null), GuestVm::class.java) as GuestVm
        fun set(name: String, value: Any) { GuestVm::class.java.getDeclaredField(name).apply { isAccessible = true }.set(vm, value) }
        val slow = object : Process() {
            override fun getOutputStream(): OutputStream = OutputStream.nullOutputStream()
            override fun getInputStream(): InputStream = InputStream.nullInputStream()
            override fun getErrorStream(): InputStream = InputStream.nullInputStream()
            override fun waitFor(): Int = 0
            override fun waitFor(timeout: Long, unit: TimeUnit): Boolean { Thread.sleep(5100); return true }
            override fun exitValue(): Int = 0
            override fun destroy() {}
            override fun destroyForcibly(): Process = this
            override fun isAlive(): Boolean = true
        }
        val exited = object : Process() {
            override fun getOutputStream(): OutputStream = OutputStream.nullOutputStream()
            override fun getInputStream(): InputStream = InputStream.nullInputStream()
            override fun getErrorStream(): InputStream = InputStream.nullInputStream()
            override fun waitFor(): Int = 0
            override fun waitFor(timeout: Long, unit: TimeUnit): Boolean = true
            override fun exitValue(): Int = 0
            override fun destroy() {}
            override fun destroyForcibly(): Process = this
            override fun isAlive(): Boolean = false
        }
        set("procs", linkedMapOf("slow" to slow, "exited" to exited))
        set("externalProcs", ConcurrentHashMap.newKeySet<Process>())
        try {
            GuestVm::class.java.getDeclaredMethod("killAll").apply { isAccessible = true }.invoke(vm)
        } catch (e: java.lang.reflect.InvocationTargetException) {
            check(e.cause is RuntimeException && e.cause?.message?.contains("not mocked") == true) { e.cause.toString() }
        }
    }

    @Test fun replacingServiceNameRetainsBothProcessesForStop() {
        val cls = Class.forName("sun.misc.Unsafe")
        val field = cls.getDeclaredField("theUnsafe").apply { isAccessible = true }
        val vm = cls.getMethod("allocateInstance", Class::class.java).invoke(field.get(null), GuestVm::class.java) as GuestVm
        fun set(name: String, value: Any) { GuestVm::class.java.getDeclaredField(name).apply { isAccessible = true }.set(vm, value) }
        set("procs", linkedMapOf<String, Process>())
        set("externalProcs", ConcurrentHashMap.newKeySet<Process>())
        val first = ProcessProbe()
        val replacement = ProcessProbe()
        val register = GuestVm::class.java.declaredMethods.firstOrNull { it.name == "trackGuestProcess" }
        assertNotNull("every started service must be tracked until it exits", register)
        register!!.apply { isAccessible = true }.invoke(vm, "service", first)
        register.invoke(vm, "service", replacement)
        try {
            GuestVm::class.java.getDeclaredMethod("killAll").apply { isAccessible = true }.invoke(vm)
        } catch (e: java.lang.reflect.InvocationTargetException) {
            check(e.cause is RuntimeException && e.cause?.message?.contains("not mocked") == true) { e.cause.toString() }
        }
        assertTrue("the replaced process was not reaped", first.waited)
        assertTrue("the current process was not reaped", replacement.waited)
    }
}
