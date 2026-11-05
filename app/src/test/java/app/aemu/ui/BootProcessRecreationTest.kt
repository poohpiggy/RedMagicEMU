package app.aemu.ui

import app.aemu.core.GuestVm
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class BootProcessRecreationTest {
    @get:Rule val temp = TemporaryFolder()

    @Test fun failedAttemptIsNotAutomaticallyRetriedAfterProcessRecreation() {
        val path = temp.newFolder()
        BootHistory(path).starting("target")
        BootHistory(path).failed("target")
        val previous = BootHistory(path).previous("target")
        assertEquals(BootHistory.Status.FAILED, previous)
        assertFalse(shouldBootOnAttach(GuestVm.State.STOPPED, previous != null))
    }

    @Test fun interruptedAttemptRequiresConfirmationButCleanStopDoesNot() {
        val path = temp.newFolder()
        BootHistory(path).starting("target")
        assertEquals(BootHistory.Status.STARTING, BootHistory(path).previous("target"))
        assertNull(BootHistory(path).previous("other"))
        BootHistory(path).stopped("target")
        assertNull(BootHistory(path).previous("target"))
        assertTrue(shouldBootOnAttach(GuestVm.State.STOPPED, false))
    }

    @Test fun stoppingAfterFailurePreservesTheFailure() {
        val history = BootHistory(temp.newFolder())
        history.starting("target")
        history.failed("target")
        history.stopped("target")
        assertEquals(BootHistory.Status.FAILED, history.previous("target"))
    }
    @Test fun failureAfterPageListenerIsRemovedIsStillRecorded() {
        val path = temp.newFolder()
        val history = BootHistory(path)
        history.starting("target")
        val recorder = BootHistoryRecorder(history, "target")
        val listeners = mutableListOf<(GuestVm.State) -> Unit>(recorder::onState)
        val page: (GuestVm.State) -> Unit = { }
        listeners.add(page)
        listeners.remove(page)
        listeners.forEach { it(GuestVm.State.FAILED) }
        assertEquals(BootHistory.Status.FAILED, BootHistory(path).previous("target"))
    }    @Test fun anotherInstanceCannotReplaceRunningVm() {
        assertTrue(rejectDifferentRunningVm("first", "second", GuestVm.State.RUNNING))
        assertFalse(rejectDifferentRunningVm("first", "first", GuestVm.State.RUNNING))
    }    @Test fun backgroundHistoryWriteFailureIsReportedWhenPageReattaches() {
        val path = temp.newFolder()
        java.io.File(path, "last-guest-boot").mkdir()
        val recorder = BootHistoryRecorder(BootHistory(path), "target")
        runCatching { recorder.onState(GuestVm.State.FAILED) }
        assertNotNull(recorder.error)
        assertTrue(showHistoryWriteErrorOnAttach(recorder))
        assertFalse(showHistoryWriteErrorOnAttach(null))
        assertTrue(java.io.File(path, "last-guest-boot").delete())
        recorder.onState(GuestVm.State.FAILED)
        assertNull(recorder.error)
        assertFalse(showHistoryWriteErrorOnAttach(recorder))
    }}
