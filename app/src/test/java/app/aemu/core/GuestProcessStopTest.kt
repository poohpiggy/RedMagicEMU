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
        set("pgids", linkedSetOf<Int>())
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
        set("pgids", linkedSetOf<Int>())
        set("externalProcs", ConcurrentHashMap.newKeySet<Process>())
        try {
            GuestVm::class.java.getDeclaredMethod("killAll").apply { isAccessible = true }.invoke(vm)
            fail("cleanup must fail while a process is still alive")
        } catch (e: java.lang.reflect.InvocationTargetException) {
            assertTrue(e.cause is IllegalStateException)
        }
        assertSame(process, procs["test"])
    }
}
