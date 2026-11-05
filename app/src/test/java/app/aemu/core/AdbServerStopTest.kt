package app.aemu.core

import org.junit.Assert.*
import org.junit.Test
import java.net.Socket
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.concurrent.CopyOnWriteArrayList

class AdbServerStopTest {
    private fun vm(): GuestVm {
        val cls = Class.forName("sun.misc.Unsafe")
        val f = cls.getDeclaredField("theUnsafe").apply { isAccessible = true }
        val vm = cls.getMethod("allocateInstance", Class::class.java).invoke(f.get(null), GuestVm::class.java) as GuestVm
        fun set(name: String, value: Any) { GuestVm::class.java.getDeclaredField(name).apply { isAccessible = true }.set(vm, value) }
        set("state", GuestVm.State.RUNNING)
        set("lifecycle", VmLifecycle().apply { begin(); endBoot() })
        set("admissionState", AdmissionState())
        set("lines", CopyOnWriteArrayList<String>())
        set("listeners", CopyOnWriteArrayList<(String) -> Unit>())
        return vm
    }
    @Test fun stoppingServerClosesAndWaitsForSyncWorker() {
        val server = AdbServer(vm())
        assertTrue(server.start())
        val socket = Socket("127.0.0.1", server.port)
        try {
            val data = "sync:\u0000".toByteArray()
            val header = ByteBuffer.allocate(24).order(ByteOrder.LITTLE_ENDIAN)
                .putInt(0x4e45504f).putInt(1).putInt(0).putInt(data.size).putInt(data.sumOf { it.toInt() and 255 }).putInt(0x4e45504f.inv()).array()
            socket.getOutputStream().write(header + data)
            val response = ByteArray(24)
            java.io.DataInputStream(socket.getInputStream()).readFully(response)
            val deadline = System.nanoTime() + java.util.concurrent.TimeUnit.SECONDS.toNanos(3)
            while (Thread.getAllStackTraces().keys.none { it.name == "adb-sync:" } && System.nanoTime() < deadline) Thread.yield()
            assertTrue(Thread.getAllStackTraces().keys.any { it.name == "adb-sync:" })
            server.stop()
            assertFalse("sync worker remains after stop", Thread.getAllStackTraces().keys.any { it.name == "adb-sync:" && it.isAlive })
        } finally { socket.close(); server.stop() }
    }
}
