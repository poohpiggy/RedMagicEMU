package app.aemu.ui

import org.junit.Assert.*
import org.junit.Test
import java.util.concurrent.CountDownLatch
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.concurrent.thread

class ImportCancellationTest {
    @Test fun cancellationBeforeImporterExistsIsDeliveredOnAttach() {
        val request = ImportCancellation()
        val importerCancelled = AtomicBoolean(false)
        request.cancel()
        assertFalse(request.attach { importerCancelled.set(true) })
        assertTrue(importerCancelled.get())
    }
    @Test fun cancellationAfterImporterExistsIsDelivered() {
        val request = ImportCancellation()
        val importerCancelled = AtomicBoolean(false)
        assertTrue(request.attach { importerCancelled.set(true) })
        request.cancel()
        assertTrue(importerCancelled.get())
    }
    @Test fun concurrentCancellationAndAttachmentCannotLoseCancellation() {
        repeat(100) {
            val request = ImportCancellation()
            val importerCancelled = AtomicBoolean(false)
            val gate = CountDownLatch(1)
            val first = thread { gate.await(); request.cancel() }
            val second = thread { gate.await(); request.attach { importerCancelled.set(true) } }
            gate.countDown(); first.join(); second.join()
            assertTrue(importerCancelled.get())
        }
    }
}
