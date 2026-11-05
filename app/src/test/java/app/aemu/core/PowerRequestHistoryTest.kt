package app.aemu.core

import java.io.IOException
import java.nio.file.Files
import java.util.concurrent.CountDownLatch
import java.util.concurrent.atomic.AtomicReference
import kotlin.concurrent.thread
import org.junit.Assert.assertNull
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeNoException
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class PowerRequestHistoryTest {
    @get:Rule val temp = TemporaryFolder()

    @Test fun guestRebootReasonSurvivesHistoryRecreation() {
        val directory = temp.newFolder()
        PowerRequestHistory(directory).record("reboot,RescueParty,detail")
        assertEquals("reboot,RescueParty,detail", PowerRequestHistory(directory).read())
    }

    @Test fun olderInstanceWithoutHistoryHasNoRequest() {
        assertNull(PowerRequestHistory(temp.newFolder()).read())
    }

    @Test fun laterPowerRequestReplacesEarlierRequest() {
        val directory = temp.newFolder()
        val history = PowerRequestHistory(directory)
        history.record("reboot,first")
        history.record("shutdown,用户选择")
        assertEquals("shutdown,用户选择", PowerRequestHistory(directory).read())
        assertEquals(listOf("last-guest-power-request"), directory.list()!!.toList())
    }

    @Test fun failedRecordLeavesPreviousRequestReadable() {
        val directory = temp.newFolder()
        val history = PowerRequestHistory(directory)
        history.record("shutdown,userrequested")
        assertThrows(IllegalArgumentException::class.java) { history.record("") }
        assertThrows(IllegalArgumentException::class.java) { history.record("x".repeat(65536)) }
        assertEquals("shutdown,userrequested", history.read())
    }

    @Test fun refusesDirectoryOrSymlinkAtHistoryPath() {
        val directory = temp.newFolder()
        val target = directory.resolve("last-guest-power-request")
        target.mkdir()
        assertThrows(IOException::class.java) { PowerRequestHistory(directory).record("shutdown") }
        assertThrows(IOException::class.java) { PowerRequestHistory(directory).read() }
        assertTrue(target.delete())
        val outside = temp.newFile()
        outside.writeText("untouched")
        try { Files.createSymbolicLink(target.toPath(), outside.toPath()) }
        catch (e: java.nio.file.FileSystemException) { assumeNoException(e) }
        assertThrows(IOException::class.java) { PowerRequestHistory(directory).record("shutdown") }
        assertThrows(IOException::class.java) { PowerRequestHistory(directory).read() }
        assertEquals("untouched", outside.readText())
    }

    @Test fun concurrentReadersSeeOnlyCompleteRequests() {
        val directory = temp.newFolder()
        val first = "shutdown," + "a".repeat(2048)
        val second = "reboot," + "b".repeat(2048)
        PowerRequestHistory(directory).record(first)
        val start = CountDownLatch(1)
        val failure = AtomicReference<Throwable?>()
        val writer = thread {
            try {
                start.await()
                repeat(30) { PowerRequestHistory(directory).record(if (it % 2 == 0) second else first) }
            } catch (e: Throwable) { failure.set(e) }
        }
        start.countDown()
        try {
            repeat(60) { assertTrue(PowerRequestHistory(directory).read() in listOf(first, second)) }
        } finally { writer.join() }
        failure.get()?.let { throw it }
    }
}
