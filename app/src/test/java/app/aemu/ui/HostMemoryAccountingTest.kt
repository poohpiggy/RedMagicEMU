package app.aemu.ui

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class HostMemoryAccountingTest {
    @Test fun androidReportReplacesTheProcessPss() {
        val memory = HostMemoryAccounting(mapOf(10 to 640L * 1024, 11 to 96L * 1024))
        memory.record(10, 320L * 1024) { error("the VM already has an Android memory report") }
        memory.record(11, 128L * 1024) { error("the UI already has an Android memory report") }
        memory.record(20, 512L * 1024) { 256L * 1024 }

        assertEquals(HostMemoryAccounting.Reading(992, true), memory.reading())
    }

    @Test fun missingOrEmptyAndroidReportsUseProcPss() {
        val memory = HostMemoryAccounting(mapOf(20 to 0, 21 to 128L * 1024))
        memory.record(20, 256L * 1024) { 64L * 1024 }
        memory.record(21, 0) { error("the process has already exited") }

        assertEquals(HostMemoryAccounting.Reading(64, true), memory.reading())
    }

    @Test fun incompleteProportionalSampleReturnsTheLabelledRssTotal() {
        val memory = HostMemoryAccounting(mapOf(10 to 640L * 1024))
        memory.record(10, 320L * 1024) { 256L * 1024 }
        memory.record(20, 128L * 1024) { null }

        assertEquals(HostMemoryAccounting.Reading(448, false), memory.reading())
        assertNull(HostMemoryAccounting(emptyMap()).reading())
    }
}
