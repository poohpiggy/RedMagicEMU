package app.aemu.core

import org.junit.Assert.*
import org.junit.Test
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import kotlin.concurrent.thread

class GuestVmStateObserverTest {
    @Test fun subscriptionDeliversSnapshotBeforeConcurrentChange() {
        val cls = Class.forName("sun.misc.Unsafe")
        val f = cls.getDeclaredField("theUnsafe").apply { isAccessible = true }
        val vm = cls.getMethod("allocateInstance", Class::class.java).invoke(f.get(null), GuestVm::class.java) as GuestVm
        fun set(name: String, value: Any) { GuestVm::class.java.getDeclaredField(name).apply { isAccessible = true }.set(vm, value) }
        set("state", GuestVm.State.STOPPED)
        set("stateListeners", CopyOnWriteArrayList<(GuestVm.State) -> Unit>())
        GuestVm::class.java.declaredFields.firstOrNull { it.name == "stateLock" }?.apply { isAccessible = true }?.set(vm, Any())
        val observe = GuestVm::class.java.methods.firstOrNull { it.name == "observeState" }
        assertNotNull("atomic snapshot subscription unavailable", observe)
        val received = CopyOnWriteArrayList<GuestVm.State>()
        val snapshotEntered = CountDownLatch(1)
        val release = CountDownLatch(1)
        val changeDone = CountDownLatch(1)
        val subscriber = thread(isDaemon = true) {
            val listener: (GuestVm.State) -> Unit = { state ->
                received.add(state)
                if (state == GuestVm.State.STOPPED) { snapshotEntered.countDown(); release.await() }
            }
            observe!!.invoke(vm, listener)
        }
        assertTrue(snapshotEntered.await(3, TimeUnit.SECONDS))
        val change = thread(isDaemon = true) {
            GuestVm::class.java.getDeclaredMethod("setState", GuestVm.State::class.java).apply { isAccessible = true }.invoke(vm, GuestVm.State.STOPPING)
            changeDone.countDown()
        }
        try { assertFalse("change overtook snapshot delivery", changeDone.await(200, TimeUnit.MILLISECONDS)) }
        finally { release.countDown(); subscriber.join(3000); change.join(3000) }
        assertEquals(listOf(GuestVm.State.STOPPED, GuestVm.State.STOPPING), received.toList())
    }
}
