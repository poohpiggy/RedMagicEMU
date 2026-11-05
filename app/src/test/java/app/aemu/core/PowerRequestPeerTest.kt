package app.aemu.core

import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class PowerRequestPeerTest {
    @get:Rule val temp = TemporaryFolder()

    @Test fun logsOnlyPeerAndGuestExecutable() {
        val procRoot = temp.newFolder()
        val proc = File(procRoot, "42").apply { mkdir() }
        File(proc, "comm").writeText("libaemu64_36.so\n")
        File(proc, "cmdline").writeBytes(
            "/data/app/libaemu64_36.so\u0000/system/bin/ztecharger\u0000secret\u0000".toByteArray()
        )
        assertEquals(
            "props: sys.powerctl peer pid=42 uid=10042 process=libaemu64_36.so guest=/system/bin/ztecharger",
            PowerRequestPeer.describe(42, 10042, procRoot)
        )
    }

    @Test fun doesNotIncludeUnexpectedArgumentsOrUntrustedPaths() {
        val procRoot = temp.newFolder()
        val proc = File(procRoot, "43").apply { mkdir() }
        File(proc, "comm").writeText("bad name\n")
        File(proc, "cmdline").writeBytes(
            "/data/app/libaemu64_36.so\u0000/data/private/secret\u0000password\u0000".toByteArray()
        )
        val text = PowerRequestPeer.describe(43, 10042, procRoot)
        assertEquals("props: sys.powerctl peer pid=43 uid=10042", text)
        assertFalse(text.contains("secret"))
        assertFalse(text.contains("password"))
    }

    @Test fun nonRunnerDoesNotTreatSecondArgumentAsExecutable() {
        val procRoot = temp.newFolder()
        val proc = File(procRoot, "44").apply { mkdir() }
        File(proc, "cmdline").writeBytes(
            "/system/bin/other\u0000/system/bin/ztecharger\u0000secret\u0000".toByteArray()
        )
        assertEquals("props: sys.powerctl peer pid=44 uid=10042", PowerRequestPeer.describe(44, 10042, procRoot))
    }

    @Test fun unavailableProcStillIdentifiesPeerCredentials() {
        assertEquals("props: sys.powerctl peer pid=99 uid=10042", PowerRequestPeer.describe(99, 10042, temp.root))
    }

    @Test fun truncatedExecutableIsNotReported() {
        val procRoot = temp.newFolder()
        val proc = File(procRoot, "45").apply { mkdir() }
        File(proc, "cmdline").writeBytes(
            "/data/app/libaemu64_36.so\u0000/system/bin/ztecharger".toByteArray()
        )
        assertEquals("props: sys.powerctl peer pid=45 uid=10042", PowerRequestPeer.describe(45, 10042, procRoot))
    }

    @Test fun unrelatedPropertyDoesNotReadPeerAndIsStillApplied() {
        var peerRead = false
        var applied = ""
        val messages = mutableListOf<String>()
        PowerRequestPeer.applyWithDiagnostics("test.key", "value", {
            peerRead = true
            "unexpected"
        }, messages::add) { key, value -> applied = "$key=$value" }
        assertFalse(peerRead)
        assertTrue(messages.isEmpty())
        assertEquals("test.key=value", applied)
    }

    @Test fun peerOrLogFailureDoesNotSuppressThePowerRequest() {
        val messages = mutableListOf<String>()
        val requests = mutableListOf<String>()
        PowerRequestPeer.applyWithDiagnostics("sys.powerctl", "shutdown,test", {
            throw SecurityException("sensitive data")
        }, messages::add) { key, value -> requests.add("$key=$value") }
        assertEquals(listOf("props: sys.powerctl peer unavailable"), messages)
        PowerRequestPeer.applyWithDiagnostics("sys.powerctl", "reboot,test", { "peer" }, {
            throw IllegalStateException("log unavailable")
        }) { key, value -> requests.add("$key=$value") }
        assertEquals(listOf("sys.powerctl=shutdown,test", "sys.powerctl=reboot,test"), requests)
    }
}
