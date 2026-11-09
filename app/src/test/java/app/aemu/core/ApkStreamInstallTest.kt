package app.aemu.core

import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.InputStream
import java.io.OutputStream
import java.io.File
import java.io.IOException
import java.util.concurrent.TimeUnit
import org.junit.Assert.*
import org.junit.Test

class ApkStreamInstallTest {
    @Test fun installGateIsExclusiveUntilReleased() {
        val gate = ApkInstallGate()
        assertTrue(gate.tryAcquire())
        assertFalse(gate.tryAcquire())
        gate.release()
        assertTrue(gate.tryAcquire())
        gate.release()
    }

    private class FakeProcess(private val text: String, private val status: Int = 0, private val completes: Boolean = true) : Process() {
        val sent = ByteArrayOutputStream()
        var killed = false
        override fun getOutputStream(): OutputStream = sent
        override fun getInputStream(): InputStream = ByteArrayInputStream(text.toByteArray())
        override fun getErrorStream(): InputStream = ByteArrayInputStream(byteArrayOf())
        override fun waitFor(): Int = status
        override fun waitFor(timeout: Long, unit: TimeUnit): Boolean = completes || killed
        override fun exitValue(): Int = status
        override fun destroy() { killed = true }
        override fun destroyForcibly(): Process { killed = true; return this }
        override fun isAlive(): Boolean = !completes && !killed
    }

    @Test fun exactBytesAndSuccessRequired() {
        val f = File.createTempFile("rme-apk-test", ".apk")
        f.writeBytes(byteArrayOf(1, 2, 3))
        try {
        val p = FakeProcess("Success\n")
        val r = runApkInstallStream(p, f, 3, 1000)
        assertTrue(r.success)
        assertEquals(0, r.exitCode)
        assertFalse(runApkInstallStream(FakeProcess("Success\n"), f, 4, 1000).success)
        val failed = runApkInstallStream(FakeProcess("Success\n", 1), f, 3, 1000)
        assertFalse(failed.success)
        assertEquals(1, failed.exitCode)
        assertFalse(runApkInstallStream(FakeProcess("Failure [error]\n"), f, 3, 1000).success)
        assertTrue(runApkInstallStream(FakeProcess("x".repeat(70000) + "\nSuccess\n"), f, 3, 1000).success)
        assertFalse(runApkInstallStream(FakeProcess("Success\nFailure [late error]\n"), f, 3, 1000).success)
        } finally { f.delete() }
    }

    @Test fun timeoutDestroysProcessAndCannotSucceed() {
        val f = File.createTempFile("rme-apk-test", ".apk")
        f.writeBytes(byteArrayOf(1))
        try {
        val p = FakeProcess("Success\n", completes = false)
        assertFalse(runApkInstallStream(p, f, 1, 10).success)
        assertTrue(p.killed)
        } finally { f.delete() }
    }

    @Test fun stagingRejectsEmptyAndReadErrors() {
        val f = File.createTempFile("rme-apk-test", ".apk")
        try {
            assertThrows(IOException::class.java) { stageApk(ByteArrayInputStream(byteArrayOf()), f) }
            assertThrows(IOException::class.java) { stageApk(object : InputStream() { override fun read(): Int = throw IOException("read failed") }, f) }
            assertEquals(3L, stageApk(ByteArrayInputStream(byteArrayOf(1, 2, 3)), f))
            assertThrows(IOException::class.java) { stageApk(ByteArrayInputStream(byteArrayOf(1, 2, 3, 4)), f, 3) }
        } finally { f.delete() }
    }
}
