package app.aemu.ui

import org.junit.Assert.*
import org.junit.Test
import java.io.IOException
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import kotlin.concurrent.thread

class ImportRecoveryNoticeTest {
    @Test fun acknowledgementWaitsForRecoveryAndUsesDisplayedToken() {
        var current: String? = "first"
        val entered = CountDownLatch(1)
        val release = CountDownLatch(1)
        var block = false
        val notice = ImportRecoveryNotice(
            recover = { if (block) { entered.countDown(); release.await() }; current },
            acknowledge = { token -> if (current == token) current = null; current }
        )
        notice.recover()
        val displayed = notice.state.value.token!!
        current = "second"
        block = true
        val recovery = thread { notice.recover() }
        assertTrue(entered.await(3, TimeUnit.SECONDS))
        val ack = thread { notice.acknowledge(displayed) }
        try { assertEquals("first", notice.state.value.token) }
        finally { release.countDown(); recovery.join(3000); ack.join(3000) }
        assertFalse(recovery.isAlive)
        assertFalse(ack.isAlive)
        assertEquals("second", notice.state.value.token)
    }

    @Test fun acknowledgingDisplayedOlderNoticeDoesNotAcknowledgeNewerOne() {
        var current: String? = "first"
        val acknowledged = mutableListOf<String>()
        val notice = ImportRecoveryNotice(
            recover = { current },
            acknowledge = { token -> acknowledged += token; if (current == token) current = null; current }
        )
        notice.recover()
        val displayed = notice.state.value.token!!
        current = "second"
        notice.recover()
        notice.acknowledge(displayed)
        assertEquals(listOf("first"), acknowledged)
        assertEquals("second", notice.state.value.token)
    }

    @Test fun recoveryAndAcknowledgementErrorsAreExposedIndependently() {
        var broken = true
        val notice = ImportRecoveryNotice(
            recover = { if (broken) throw IOException("cleanup failed") else "first" },
            acknowledge = { throw IOException("ack failed") }
        )
        notice.recover()
        assertEquals("cleanup failed", notice.state.value.error)
        broken = false
        notice.recover()
        assertNull(notice.state.value.error)
        notice.acknowledge("first")
        assertEquals("ack failed", notice.state.value.error)
        assertEquals("first", notice.state.value.token)
    }
}
