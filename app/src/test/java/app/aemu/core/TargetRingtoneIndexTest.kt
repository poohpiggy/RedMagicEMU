package app.aemu.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class TargetRingtoneIndexTest {
    private val paths = listOf(
        "/product/media/audio/ringtones/1Alpha_5.ogg",
        "/product/media/audio/notifications/1Bounce.ogg",
        "/product/media/audio/alarms/1Quantum_Foam.ogg",
    )
    private fun row(path: String) = 0 to "Row: 0 _data=$path\n"

    @Test fun otherGuestsNeverRunAnyCommand() {
        for ((api, model, abi, engine) in listOf(
            Quad(35, "NX809J", "arm64-v8a", Engine.N64),
            Quad(36, "other", "arm64-v8a", Engine.N64),
            Quad(36, "NX809J", "armeabi-v7a", Engine.N64),
            Quad(36, "NX809J", "arm64-v8a", Engine.KK),
        )) {
            val result = TargetRingtoneIndex.ensure(api, model, abi, engine) { _, _ ->
                throw AssertionError("non-target executed command")
            }
            assertEquals(TargetRingtoneIndex.Result.NOT_APPLICABLE, result)
        }
    }

    @Test fun existingEntriesStillScanTheSystemDirectory() {
        val calls = mutableListOf<String>()
        val outputs = ArrayDeque(listOf(
            0 to "Result: Bundle[{android.intent.extra.STREAM=content://media/internal/audio/media/5}]\n",
            row(paths[0]), row(paths[1]), row(paths[2]),
        ))
        val result = TargetRingtoneIndex.ensure(36, "NX809J", "arm64-v8a", Engine.N64) { cmd, _ ->
            calls += cmd
            outputs.removeFirst()
        }
        assertEquals(TargetRingtoneIndex.Result.SCANNED, result)
        assertEquals(4, calls.size)
        assertEquals("content call --uri content://media --method scan_file --arg /product/media/audio", calls[0])
        assertFalse(calls.any { it.contains("settings") || it.contains("/storage/") })
    }

    @Test fun missingEntryScansOnlySystemAudioThenVerifiesAll() {
        val calls = mutableListOf<String>()
        val outputs = ArrayDeque(listOf(
            0 to "Result: Bundle[{android.intent.extra.STREAM=content://media/internal/audio/media/5}]\n",
            row(paths[0]), row(paths[1]), row(paths[2]),
        ))
        val result = TargetRingtoneIndex.ensure(36, "NX809J", "arm64-v8a", Engine.N64) { cmd, timeout ->
            calls += cmd
            assertTrue(timeout > 0 && timeout <= 60_000)
            outputs.removeFirst()
        }
        assertEquals(TargetRingtoneIndex.Result.SCANNED, result)
        assertEquals(4, calls.size)
        assertEquals("content call --uri content://media --method scan_file --arg /product/media/audio", calls[0])
        assertFalse(calls.any { it.contains("settings") || it.contains("/storage/") })
    }

    @Test fun emptyBundleDoesNotPretendScanSucceeded() {
        val outputs = ArrayDeque(listOf(0 to "Result: Bundle[{}]\n"))
        val result = TargetRingtoneIndex.ensure(36, "NX809J", "arm64-v8a", Engine.N64) { _, _ -> outputs.removeFirst() }
        assertEquals(TargetRingtoneIndex.Result.FAILED, result)
    }

    @Test fun android16ParceledStreamIsAcceptedAfterCheckingAllDefaults() {
        val outputs = ArrayDeque(listOf(
            0 to "Result: Bundle[{android.intent.extra.STREAM=Supplier{VAL_PARCELABLE@64+116}}]\n",
            row(paths[0]), row(paths[1]), row(paths[2]),
        ))
        val result = TargetRingtoneIndex.ensure(36, "NX809J", "arm64-v8a", Engine.N64) { _, _ -> outputs.removeFirst() }
        assertEquals(TargetRingtoneIndex.Result.SCANNED, result)
    }

    @Test fun scanStreamMixedWithExceptionDoesNotReportSuccess() {
        var calls = 0
        val events = mutableListOf<String>()
        val result = TargetRingtoneIndex.ensure(36, "NX809J", "arm64-v8a", Engine.N64, events::add) { _, _ ->
            calls++
            if (calls == 1) 0 to "Result: Bundle[{android.intent.extra.STREAM=Supplier{VAL_PARCELABLE@64+116}}]\njava.lang.SecurityException: private-data\n"
            else row(paths[calls - 2])
        }
        assertEquals(TargetRingtoneIndex.Result.FAILED, result)
        assertEquals(1, calls)
        assertTrue(events.contains("scan exception: SecurityException"))
        assertFalse(events.any { it.contains("private-data") })
    }

    @Test fun verificationRowMixedWithExceptionDoesNotReportSuccess() {
        val outputs = ArrayDeque(listOf(
            0 to "Result: Bundle[{android.intent.extra.STREAM=Supplier{VAL_PARCELABLE@64+116}}]",
            0 to "Row: 0 _data=${paths[0]}\njava.lang.SecurityException: private-data\n",
            row(paths[1]), row(paths[2]),
        ))
        val result = TargetRingtoneIndex.ensure(36, "NX809J", "arm64-v8a", Engine.N64) { _, _ -> outputs.removeFirst() }
        assertEquals(TargetRingtoneIndex.Result.FAILED, result)
    }

    @Test fun malformedOrMissingParceledStreamIsRejected() {
        for (response in listOf(
            "Result: Bundle[{android.intent.extra.STREAM=null}]\n",
            "Result: Bundle[{android.intent.extra.STREAM=Supplier{VAL_PARCELABLE@bad}}]\n",
            "android.intent.extra.STREAM=Supplier{VAL_PARCELABLE@64+116}\n",
        )) {
            var calls = 0
            val result = TargetRingtoneIndex.ensure(36, "NX809J", "arm64-v8a", Engine.N64) { _, _ -> calls++; 0 to response }
            assertEquals(TargetRingtoneIndex.Result.FAILED, result)
            assertEquals(1, calls)
        }
    }

    @Test fun scanFailureDoesNotTriggerQueries() {
        val calls = mutableListOf<String>()
        val result = TargetRingtoneIndex.ensure(36, "NX809J", "arm64-v8a", Engine.N64) { cmd, _ ->
            calls += cmd
            1 to "provider unavailable"
        }
        assertEquals(TargetRingtoneIndex.Result.FAILED, result)
        assertEquals(1, calls.size)
    }

    @Test fun unresolvedAfterScanIsFailure() {
        val outputs = ArrayDeque(listOf(
            0 to "Result: Bundle[{android.intent.extra.STREAM=content://media/internal/audio/media/5}]\n",
            0 to "No result found.\n", row(paths[1]), row(paths[2]),
        ))
        val result = TargetRingtoneIndex.ensure(36, "NX809J", "arm64-v8a", Engine.N64) { _, _ -> outputs.removeFirst() }
        assertEquals(TargetRingtoneIndex.Result.FAILED, result)
    }

    @Test fun transientProviderFailureIsRetriedAndReportsRecovery() {
        val outputs = ArrayDeque(listOf(
            0 to "Exception occurred while executing call",
            0 to "No result found.\n",
            0 to "Result: Bundle[{android.intent.extra.STREAM=Supplier{VAL_PARCELABLE@64+116}}]",
            row(paths[0]), row(paths[1]), row(paths[2]),
        ))
        val delays = mutableListOf<Long>()
        val events = mutableListOf<String>()
        val result = TargetRingtoneIndex.ensureWithRetry(36, "NX809J", "arm64-v8a", Engine.N64,
            { delay -> delays += delay; true }, events::add) { _, _ -> outputs.removeFirst() }
        assertEquals(TargetRingtoneIndex.Result.SCANNED, result)
        assertEquals(listOf(5000L), delays)
        assertTrue(events.any { it.contains("provider query not ready") })
        assertTrue(events.any { it.contains("attempt 2") })
    }

    @Test fun persistentFailureStopsAfterThreeAttempts() {
        var commands = 0
        var pauses = 0
        val events = mutableListOf<String>()
        val result = TargetRingtoneIndex.ensureWithRetry(36, "NX809J", "arm64-v8a", Engine.N64,
            { pauses++; true }, events::add) { _, _ -> commands++; -9 to "" }
        assertEquals(TargetRingtoneIndex.Result.FAILED, result)
        assertEquals(3, commands)
        assertEquals(2, pauses)
        assertTrue(events.any { it.contains("all attempts failed") })
    }

    @Test fun stopDuringDelayCancelsFurtherAttempts() {
        var commands = 0
        val events = mutableListOf<String>()
        val result = TargetRingtoneIndex.ensureWithRetry(36, "NX809J", "arm64-v8a", Engine.N64,
            { false }, events::add) { _, _ -> commands++; 1 to "provider unavailable" }
        assertEquals(TargetRingtoneIndex.Result.CANCELLED, result)
        assertEquals(1, commands)
        assertTrue(events.any { it.contains("cancelled") })
    }

    @Test fun queryFailureReportsItsStageAndDoesNotPretendSuccess() {
        val events = mutableListOf<String>()
        val outputs = ArrayDeque(listOf(
            0 to "Result: Bundle[{android.intent.extra.STREAM=Supplier{VAL_PARCELABLE@64+116}}]",
            1 to "provider unavailable",
        ))
        val result = TargetRingtoneIndex.ensure(36, "NX809J", "arm64-v8a", Engine.N64,
            events::add) { _, _ -> outputs.removeFirst() }
        assertEquals(TargetRingtoneIndex.Result.FAILED, result)
        assertTrue(events.any { it.contains("ringtone query failed") })
    }

    @Test(expected = IllegalStateException::class)
    fun commandExceptionIsNotSwallowed() {
        TargetRingtoneIndex.ensureWithRetry(36, "NX809J", "arm64-v8a", Engine.N64,
            { true }, {}) { _, _ -> throw IllegalStateException("command failed") }
    }

    @Test fun unsuccessfulOrTimedOutScanNeverRetries() {
        for (code in listOf(-9, -1, 1, 137)) {
            var commands = 0
            var pauses = 0
            val events = mutableListOf<String>()
            val result = TargetRingtoneIndex.ensureWithRetry(36, "NX809J", "arm64-v8a", Engine.N64,
                { pauses++; true }, events::add) { _, _ ->
                commands++
                if (commands == 1) 0 to "No result found.\n" else code to ""
            }
            assertEquals(TargetRingtoneIndex.Result.FAILED, result)
            assertEquals(2, commands)
            assertEquals(0, pauses)
            assertTrue(events.any { it.contains("scan command failed") })
        }
    }

    @Test fun queryFailureAfterSuccessfulScanNeverRetries() {
        var calls = 0
        var pauses = 0
        val result = TargetRingtoneIndex.ensureWithRetry(36, "NX809J", "arm64-v8a", Engine.N64,
            { pauses++; true }, {}) { _, _ ->
            calls++
            when (calls) {
                1 -> row(paths[0])
                2 -> 0 to "Result: Bundle[{android.intent.extra.STREAM=Supplier{VAL_PARCELABLE@64+116}}]"
                else -> 1 to "query failed"
            }
        }
        assertEquals(TargetRingtoneIndex.Result.FAILED, result)
        assertEquals(3, calls)
        assertEquals(0, pauses)
    }

    @Test fun readyExistingIndexIsProbedReadOnlyBeforeOneScan() {
        val calls = mutableListOf<String>()
        val outputs = ArrayDeque(listOf(
            row(paths[0]),
            0 to "Result: Bundle[{android.intent.extra.STREAM=Supplier{VAL_PARCELABLE@64+116}}]",
            row(paths[0]), row(paths[1]), row(paths[2]),
        ))
        val result = TargetRingtoneIndex.ensureWithRetry(36, "NX809J", "arm64-v8a", Engine.N64,
            { throw AssertionError("already ready") }, {}) { command, _ -> calls += command; outputs.removeFirst() }
        assertEquals(TargetRingtoneIndex.Result.SCANNED, result)
        assertEquals(5, calls.size)
        assertTrue(calls[0].startsWith("content query "))
        assertEquals(1, calls.count { it.contains("--method scan_file") })
        assertFalse(calls.any { it.contains("settings") || it.contains("/storage/") })
    }

    @Test fun emptyScanResultDoesNotRepeatAfterReadiness() {
        val calls = mutableListOf<String>()
        val outputs = ArrayDeque(listOf(0 to "No result found.\n", 0 to "Result: Bundle[{}]"))
        val result = TargetRingtoneIndex.ensureWithRetry(36, "NX809J", "arm64-v8a", Engine.N64,
            { throw AssertionError("scan must not retry") }, {}) { command, _ -> calls += command; outputs.removeFirst() }
        assertEquals(TargetRingtoneIndex.Result.FAILED, result)
        assertEquals(1, calls.count { it.contains("--method scan_file") })
    }

    @Test fun providerResponseWithExceptionIsNotAcceptedAsReady() {
        var calls = 0
        val result = TargetRingtoneIndex.ensureWithRetry(36, "NX809J", "arm64-v8a", Engine.N64,
            { false }, {}) { _, _ -> calls++; 0 to "No result found.\nException: provider failed\n" }
        assertEquals(TargetRingtoneIndex.Result.CANCELLED, result)
        assertEquals(1, calls)
    }

    @Test fun scanExceptionClassificationUsesOnlyFixedKnownTypes() {
        val classes = listOf("SecurityException", "IllegalStateException", "IllegalArgumentException",
            "NullPointerException", "RemoteException", "RuntimeException")
        for (name in classes + "CustomException") {
            val events = mutableListOf<String>()
            val output = "Exception occurred while executing call: $name: private-data"
            val result = TargetRingtoneIndex.ensure(36, "NX809J", "arm64-v8a", Engine.N64,
                events::add) { _, _ -> 0 to output }
            assertEquals(TargetRingtoneIndex.Result.FAILED, result)
            val expected = if (name == "CustomException") "other" else name
            assertTrue(events.contains("scan exception: $expected"))
            assertFalse(events.any { it.contains("private-data") })
        }
    }

    @Test fun scanFailureLogsOnlyFixedResponseCategories() {
        for ((output, category) in listOf(
            "Result: Bundle[{}]\n" to "empty bundle",
            "Result: null\n" to "null result",
            "Exception occurred while executing: private-data\n" to "exception text",
            "unrecognized private-data\n" to "other",
        )) {
            val events = mutableListOf<String>()
            val result = TargetRingtoneIndex.ensure(36, "NX809J", "arm64-v8a", Engine.N64,
                events::add) { _, _ -> 0 to output }
            assertEquals(TargetRingtoneIndex.Result.FAILED, result)
            assertTrue(events.contains("scan response: $category"))
            assertFalse(events.any { it.contains("private-data") })
        }
    }

    private data class Quad(val api: Int, val model: String, val abi: String, val engine: Engine)
}
