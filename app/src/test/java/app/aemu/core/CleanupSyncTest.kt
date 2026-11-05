package app.aemu.core

import org.junit.Assert.*
import org.junit.Test
import java.io.File
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.TimeUnit

class CleanupSyncTest {
    @Test fun cleanupSyncProcessRemainsTrackedUntilGuestCleanup() {
        val unsafeClass = Class.forName("sun.misc.Unsafe")
        val field = unsafeClass.getDeclaredField("theUnsafe").apply { isAccessible = true }
        val vm = unsafeClass.getMethod("allocateInstance", Class::class.java)
            .invoke(field.get(null), GuestVm::class.java) as GuestVm
        val tracked = ConcurrentHashMap.newKeySet<Process>()
        GuestVm::class.java.getDeclaredField("externalProcs").apply { isAccessible = true }.set(vm, tracked)
        val executable = File(System.getProperty("java.home"), "bin/java" + if (System.getProperty("os.name").startsWith("Windows")) ".exe" else "")
        val child = vm.startCleanupSync(ProcessBuilder(executable.absolutePath, "-version").redirectErrorStream(true))
        try {
            assertTrue("cleanup sync must remain tracked for killAll", tracked.contains(child))
            assertTrue(child.waitFor(10, TimeUnit.SECONDS))
            assertTrue("finished sync is retained until cleanup accounts for it", tracked.contains(child))
        } finally { child.destroyForcibly(); child.waitFor(10, TimeUnit.SECONDS) }
    }
}
