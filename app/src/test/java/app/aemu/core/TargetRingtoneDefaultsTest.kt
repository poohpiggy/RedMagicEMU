package app.aemu.core

import org.junit.Assert.*
import org.junit.Test
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger
import kotlin.concurrent.thread

class TargetRingtoneDefaultsTest {
    private val eligible = TargetRingtoneDefaults.Prerequisites(
        api = 36, model = "NX809J", abi = "arm64-v8a", engine = Engine.N64,
        controlledFreshInstance = true, systemAudioIndexed = true, authorizedVolumeReady = true,
    )

    private val responses = mapOf(
        "content call --uri content://media --method scan_volume --arg external_primary" to "Result: Bundle[{}]",
        "settings get system ringtone" to "content://media/internal/audio/media/85?title=Alpha%205&canonical=1",
        "content query --uri 'content://media/internal/audio/media/85?title=Alpha%205&canonical=1' --projection _data --where \"is_ringtone=1\"" to
            "Row: 0 _data=/product/media/audio/ringtones/1Alpha_5.ogg",
        "settings get system ringtone_set" to "1",
        "settings get system notification_sound" to "content://media/internal/audio/media/47?title=Bounce&canonical=1",
        "content query --uri 'content://media/internal/audio/media/47?title=Bounce&canonical=1' --projection _data --where \"is_notification=1\"" to
            "Row: 0 _data=/product/media/audio/notifications/1Bounce.ogg",
        "settings get system notification_sound_set" to "1",
        "settings get system alarm_alert" to "content://media/internal/audio/media/5?title=Quantum%20Foam&canonical=1",
        "content query --uri 'content://media/internal/audio/media/5?title=Quantum%20Foam&canonical=1' --projection _data --where \"is_alarm=1\"" to
            "Row: 0 _data=/product/media/audio/alarms/1Quantum_Foam.ogg",
        "settings get system alarm_alert_set" to "1",
    )

    private fun response(command: String): TargetRingtoneDefaults.CommandResult =
        TargetRingtoneDefaults.CommandResult.Completed(0, responses.getValue(command), outputComplete = true)

    @Test fun requiresTargetAndAllExplicitSafetyPrerequisites() {
        val cases = listOf(
            eligible.copy(api = 35), eligible.copy(model = "other"), eligible.copy(abi = "armeabi-v7a"),
            eligible.copy(engine = Engine.KK), eligible.copy(controlledFreshInstance = false),
            eligible.copy(systemAudioIndexed = false), eligible.copy(authorizedVolumeReady = false),
        )
        for (prerequisites in cases) {
            assertEquals(TargetRingtoneDefaults.Result.NotEligible,
                TargetRingtoneDefaults().execute(prerequisites) { _, _ -> fail("must not execute"); error("unreachable") })
        }
    }

    @Test fun emptyScanBundleRequiresAllThreeFilesCategoriesAndSetFlags() {
        val calls = mutableListOf<String>()
        val operation = TargetRingtoneDefaults()
        val result = operation.execute(eligible) { command, timeout ->
            assertTrue(timeout in 1..60_000)
            calls += command
            response(command)
        }
        assertEquals(TargetRingtoneDefaults.Result.ObservedMatchingDefaults, result)
        assertEquals(responses.keys.toList(), calls)
        assertEquals(TargetRingtoneDefaults.Result.AlreadyAttempted,
            operation.execute(eligible) { _, _ -> fail("must not repeat"); error("unreachable") })
    }

    @Test fun emptyBundleAloneIsNotSuccessWhenASettingIsNull() {
        val operation = TargetRingtoneDefaults()
        val result = operation.execute(eligible) { command, _ ->
            if (command == "settings get system ringtone") TargetRingtoneDefaults.CommandResult.Completed(0, "null", outputComplete = true)
            else if (command.startsWith("content query --uri content://settings/system"))
                TargetRingtoneDefaults.CommandResult.Completed(0, "No result found.", outputComplete = true)
            else response(command)
        }
        assertEquals(TargetRingtoneDefaults.Result.Uncertain(TargetRingtoneDefaults.Stage.VERIFY,
            TargetRingtoneDefaults.Reason.INVALID_OUTPUT), result)
        assertEquals(TargetRingtoneDefaults.Result.AlreadyAttempted,
            operation.execute(eligible) { _, _ -> fail("must not repeat"); error("unreachable") })
    }

    @Test fun timeoutAndExceptionsAreUncertainAndNeverRetried() {
        for (timedOut in listOf(true, false)) {
            val operation = TargetRingtoneDefaults()
            var calls = 0
            val result = operation.execute(eligible) { _, _ ->
                calls++
                if (timedOut) TargetRingtoneDefaults.CommandResult.TimedOut
                else throw IllegalStateException("private output must not be reported")
            }
            assertEquals(TargetRingtoneDefaults.Result.Uncertain(TargetRingtoneDefaults.Stage.SCAN,
                if (timedOut) TargetRingtoneDefaults.Reason.TIMEOUT else TargetRingtoneDefaults.Reason.EXCEPTION), result)
            assertEquals(TargetRingtoneDefaults.Result.AlreadyAttempted,
                operation.execute(eligible) { _, _ -> fail("must not repeat"); error("unreachable") })
            assertEquals(1, calls)
        }
    }

    @Test fun nonzeroOrMalformedScanResponseCannotSucceed() {
        for (output in listOf(
            TargetRingtoneDefaults.CommandResult.Completed(9, "Result: Bundle[{}]", outputComplete = true),
            TargetRingtoneDefaults.CommandResult.Completed(0, "Result: Bundle[{}]\nError: provider failed", outputComplete = true),
            TargetRingtoneDefaults.CommandResult.Completed(0, "Result: null", outputComplete = true),
        )) {
            var calls = 0
            val result = TargetRingtoneDefaults().execute(eligible) { _, _ -> calls++; output }
            assertTrue(result is TargetRingtoneDefaults.Result.Uncertain)
            assertEquals(TargetRingtoneDefaults.Stage.SCAN, (result as TargetRingtoneDefaults.Result.Uncertain).stage)
            assertEquals(1, calls)
        }
    }

    @Test fun verificationTimeoutIsUncertainAfterSingleScan() {
        val operation = TargetRingtoneDefaults()
        var calls = 0
        val result = operation.execute(eligible) { command, _ ->
            calls++
            if (command == "settings get system notification_sound") TargetRingtoneDefaults.CommandResult.TimedOut
            else response(command)
        }
        assertEquals(TargetRingtoneDefaults.Result.Uncertain(TargetRingtoneDefaults.Stage.VERIFY,
            TargetRingtoneDefaults.Reason.TIMEOUT), result)
        assertEquals(5, calls)
        assertEquals(TargetRingtoneDefaults.Result.AlreadyAttempted,
            operation.execute(eligible) { _, _ -> fail("must not repeat"); error("unreachable") })
    }

    @Test fun verificationCanResumeAfterReadTimeoutWithoutRepeatingScan() {
        val operation = TargetRingtoneDefaults()
        val first = operation.execute(eligible) { command, _ ->
            if (command == "settings get system notification_sound") TargetRingtoneDefaults.CommandResult.TimedOut
            else response(command)
        }
        assertEquals(TargetRingtoneDefaults.Result.Uncertain(TargetRingtoneDefaults.Stage.VERIFY,
            TargetRingtoneDefaults.Reason.TIMEOUT), first)
        val calls = mutableListOf<String>()
        assertEquals(TargetRingtoneDefaults.Result.ObservedMatchingDefaults,
            operation.verifyObserved(eligible) { command, _ -> calls += command; response(command) })
        assertEquals(responses.keys.filterNot { it.startsWith("content call") }, calls)
    }

    @Test fun verificationOfUserSilenceDoesNotChangeSettingsOrScan() {
        val calls = mutableListOf<String>()
        val result = TargetRingtoneDefaults().verifyObserved(eligible) { command, _ ->
            calls += command
            if (command == "settings get system ringtone")
                TargetRingtoneDefaults.CommandResult.Completed(0, "null", outputComplete = true)
            else if (command == "content query --uri content://settings/system --projection name --where \"name='ringtone'\"")
                TargetRingtoneDefaults.CommandResult.Completed(0, "No result found.", outputComplete = true)
            else response(command)
        }
        assertEquals(TargetRingtoneDefaults.Result.Uncertain(TargetRingtoneDefaults.Stage.VERIFY,
            TargetRingtoneDefaults.Reason.INVALID_OUTPUT), result)
        assertEquals(listOf("settings get system ringtone", "settings get system ringtone_set",
            "content query --uri content://settings/system --projection name --where \"name='ringtone'\""), calls)
    }

    @Test fun initializedSilenceAndCustomRingtoneAreRecognizedWithoutWritingSettings() {
        for (ringtone in listOf("null", "content://example.provider/custom/12")) {
            val calls = mutableListOf<String>()
            val result = TargetRingtoneDefaults().verifyObserved(eligible) { command, _ ->
                calls += command
                when (command) {
                    "settings get system ringtone" -> TargetRingtoneDefaults.CommandResult.Completed(0, ringtone, true)
                    "content query --uri content://settings/system --projection name --where \"name='ringtone'\"" ->
                        TargetRingtoneDefaults.CommandResult.Completed(0, "Row: 0 name=ringtone", true)
                    "content query --uri content://settings/system --projection name --where \"name='notification_sound'\"" ->
                        TargetRingtoneDefaults.CommandResult.Completed(0, "Row: 0 name=notification_sound", true)
                    "content query --uri content://settings/system --projection name --where \"name='alarm_alert'\"" ->
                        TargetRingtoneDefaults.CommandResult.Completed(0, "Row: 0 name=alarm_alert", true)
                    else -> response(command)
                }
            }
            assertEquals(TargetRingtoneDefaults.Result.ConfiguredWithUserChoices, result)
            assertTrue(calls.none { it.startsWith("content call") || it.startsWith("settings put") })
            assertTrue(calls.none { it.contains("example.provider") })
        }
    }

    @Test fun initializedChoiceNeedsAllThreeFlagsAndExistingSettingsRows() {
        for ((commandToBreak, brokenOutput) in listOf(
            "settings get system notification_sound_set" to "0",
            "content query --uri content://settings/system --projection name --where \"name='alarm_alert'\"" to "No result found.",
            "content query --uri content://settings/system --projection name --where \"name='alarm_alert'\"" to "Row: 0 name=alarm_alert\nRow: 1 name=alarm_alert",
        )) {
            val result = TargetRingtoneDefaults().verifyObserved(eligible) { command, _ ->
                val output = when (command) {
                    commandToBreak -> brokenOutput
                    "settings get system ringtone" -> "null"
                    "content query --uri content://settings/system --projection name --where \"name='ringtone'\"" -> "Row: 0 name=ringtone"
                    "content query --uri content://settings/system --projection name --where \"name='notification_sound'\"" -> "Row: 0 name=notification_sound"
                    "content query --uri content://settings/system --projection name --where \"name='alarm_alert'\"" -> "Row: 0 name=alarm_alert"
                    else -> return@verifyObserved response(command)
                }
                TargetRingtoneDefaults.CommandResult.Completed(0, output, true)
            }
            assertTrue(result is TargetRingtoneDefaults.Result.Uncertain)
        }
    }

    @Test fun missingMediaRowIsNotProofOfAUserChoice() {
        val result = TargetRingtoneDefaults().verifyObserved(eligible) { command, _ ->
            val output = when (command) {
                "content query --uri 'content://media/internal/audio/media/85?title=Alpha%205&canonical=1' --projection _data --where \"is_ringtone=1\"" -> "No result found."
                "content query --uri content://settings/system --projection name --where \"name='ringtone'\"" -> "Row: 0 name=ringtone"
                "content query --uri content://settings/system --projection name --where \"name='notification_sound'\"" -> "Row: 0 name=notification_sound"
                "content query --uri content://settings/system --projection name --where \"name='alarm_alert'\"" -> "Row: 0 name=alarm_alert"
                else -> return@verifyObserved response(command)
            }
            TargetRingtoneDefaults.CommandResult.Completed(0, output, true)
        }
        assertEquals(TargetRingtoneDefaults.Result.Uncertain(TargetRingtoneDefaults.Stage.VERIFY,
            TargetRingtoneDefaults.Reason.INVALID_OUTPUT), result)
    }

    @Test fun oneChoiceDoesNotHideAnotherMissingMediaRow() {
        val result = TargetRingtoneDefaults().verifyObserved(eligible) { command, _ ->
            val output = when (command) {
                "settings get system ringtone" -> "null"
                "content query --uri 'content://media/internal/audio/media/47?title=Bounce&canonical=1' --projection _data --where \"is_notification=1\"" -> "No result found."
                "content query --uri content://settings/system --projection name --where \"name='ringtone'\"" -> "Row: 0 name=ringtone"
                "content query --uri content://settings/system --projection name --where \"name='notification_sound'\"" -> "Row: 0 name=notification_sound"
                "content query --uri content://settings/system --projection name --where \"name='alarm_alert'\"" -> "Row: 0 name=alarm_alert"
                else -> return@verifyObserved response(command)
            }
            TargetRingtoneDefaults.CommandResult.Completed(0, output, true)
        }
        assertEquals(TargetRingtoneDefaults.Result.Uncertain(TargetRingtoneDefaults.Stage.VERIFY,
            TargetRingtoneDefaults.Reason.INVALID_OUTPUT), result)
    }

    @Test fun malformedInternalMediaUriIsNotTreatedAsAnExternalChoice() {
        for (invalid in listOf("content://media/internal/audio/media/0", "content://media/internal/audio/media/12345678901234567890",
            "content://media/internal/audio/media", "content://media/internal/audio/media?x=1", "content://media/internal/audio/media/")) {
            val result = TargetRingtoneDefaults().verifyObserved(eligible) { command, _ ->
                val output = when (command) {
                    "settings get system ringtone" -> invalid
                    "content query --uri content://settings/system --projection name --where \"name='ringtone'\"" -> "Row: 0 name=ringtone"
                    "content query --uri content://settings/system --projection name --where \"name='notification_sound'\"" -> "Row: 0 name=notification_sound"
                    "content query --uri content://settings/system --projection name --where \"name='alarm_alert'\"" -> "Row: 0 name=alarm_alert"
                    else -> return@verifyObserved response(command)
                }
                TargetRingtoneDefaults.CommandResult.Completed(0, output, true)
            }
            assertTrue(result is TargetRingtoneDefaults.Result.Uncertain)
        }
    }

    @Test fun selectionRevertedToDefaultsDuringVerificationIsReportedAsDefault() {
        var ringtoneReads = 0
        val result = TargetRingtoneDefaults().verifyObserved(eligible) { command, _ ->
            val output = when (command) {
                "settings get system ringtone" -> if (ringtoneReads++ == 0) "null" else responses.getValue(command)
                "content query --uri content://settings/system --projection name --where \"name='ringtone'\"" -> "Row: 0 name=ringtone"
                "content query --uri content://settings/system --projection name --where \"name='notification_sound'\"" -> "Row: 0 name=notification_sound"
                "content query --uri content://settings/system --projection name --where \"name='alarm_alert'\"" -> "Row: 0 name=alarm_alert"
                else -> return@verifyObserved response(command)
            }
            TargetRingtoneDefaults.CommandResult.Completed(0, output, true)
        }
        assertEquals(TargetRingtoneDefaults.Result.ObservedMatchingDefaults, result)
    }

    @Test fun readOnlyVerificationStillRequiresTargetAndIndexedSystemAudio() {
        for (p in listOf(eligible.copy(api = 35), eligible.copy(controlledFreshInstance = false),
                eligible.copy(systemAudioIndexed = false))) {
            assertEquals(TargetRingtoneDefaults.Result.NotEligible,
                TargetRingtoneDefaults().verifyObserved(p) { _, _ -> fail("must not query"); error("unreachable") })
        }
        assertEquals(TargetRingtoneDefaults.Result.ObservedMatchingDefaults,
            TargetRingtoneDefaults().verifyObserved(eligible.copy(authorizedVolumeReady = false)) { command, _ -> response(command) })
    }

    @Test fun linkerWarningsDoNotHideValidSingleResult() {
        val result = TargetRingtoneDefaults().execute(eligible) { command, _ ->
            val value = (response(command) as TargetRingtoneDefaults.CommandResult.Completed).output
            TargetRingtoneDefaults.CommandResult.Completed(0, "WARNING: linker: harmless\n$value\n", outputComplete = true)
        }
        assertEquals(TargetRingtoneDefaults.Result.ObservedMatchingDefaults, result)
    }

    @Test fun deviceSizedLinkerWarningsStillAllowAnExactResult() {
        val command = "content query --uri 'content://media/internal/audio/media/85?title=Alpha%205&canonical=1' --projection _data --where \"is_ringtone=1\""
        val row = responses.getValue(command)
        val output = buildString {
            repeat(29) { append("WARNING: linker: "); append("x".repeat(830)); append('\n') }
            append("WARNING: linker: ")
            append("x".repeat(25_549 - length - 1 - row.length))
            append('\n'); append(row)
        }
        assertEquals(25_549, output.length)
        val result = TargetRingtoneDefaults().execute(eligible) { requested, _ ->
            if (requested == command) TargetRingtoneDefaults.CommandResult.Completed(0, output, outputComplete = true)
            else response(requested)
        }
        assertEquals(TargetRingtoneDefaults.Result.ObservedMatchingDefaults, result)
    }

    @Test fun completeLookingTruncatedOutputIsStillUncertain() {
        for (broken in listOf("content call --uri content://media --method scan_volume --arg external_primary", "settings get system ringtone")) {
            val operation = TargetRingtoneDefaults()
            val result = operation.execute(eligible) { command, _ ->
                val successful = response(command) as TargetRingtoneDefaults.CommandResult.Completed
                successful.copy(outputComplete = command != broken)
            }
            assertEquals(TargetRingtoneDefaults.Result.Uncertain(
                if (broken.startsWith("content call")) TargetRingtoneDefaults.Stage.SCAN else TargetRingtoneDefaults.Stage.VERIFY,
                TargetRingtoneDefaults.Reason.OUTPUT_INCOMPLETE), result)
            assertEquals(TargetRingtoneDefaults.Result.AlreadyAttempted,
                operation.execute(eligible) { _, _ -> fail("must not repeat"); error("unreachable") })
        }
    }

    @Test fun outputAboveSafetyBoundCannotSucceed() {
        val result = TargetRingtoneDefaults().execute(eligible) { _, _ ->
            TargetRingtoneDefaults.CommandResult.Completed(0, "x".repeat(65_537), outputComplete = true)
        }
        assertEquals(TargetRingtoneDefaults.Result.Uncertain(TargetRingtoneDefaults.Stage.SCAN,
            TargetRingtoneDefaults.Reason.INVALID_OUTPUT), result)
    }

    @Test fun partialSettingsWrongPathAndUnsetFlagAreUncertain() {
        val invalid = mapOf(
            "settings get system notification_sound" to "null",
            "content query --uri 'content://media/internal/audio/media/85?title=Alpha%205&canonical=1' --projection _data --where \"is_ringtone=1\"" to
                "Row: 0 _data=/product/media/audio/ringtones/other.ogg",
            "settings get system alarm_alert_set" to "0",
        )
        for ((broken, value) in invalid) {
            val result = TargetRingtoneDefaults().execute(eligible) { command, _ ->
                if (command == broken) TargetRingtoneDefaults.CommandResult.Completed(0, value, outputComplete = true)
                else if (command.startsWith("content query --uri content://settings/system"))
                    TargetRingtoneDefaults.CommandResult.Completed(0, "No result found.", outputComplete = true)
                else response(command)
            }
            assertEquals(TargetRingtoneDefaults.Result.Uncertain(TargetRingtoneDefaults.Stage.VERIFY,
                TargetRingtoneDefaults.Reason.INVALID_OUTPUT), result)
        }
    }

    @Test fun injectionAndAmbiguousOrErrorMixedOutputAreRejected() {
        for (value in listOf(
            "content://media/internal/audio/media/85;touch /tmp/bad",
            "content://media/internal/audio/media/85\ncontent://media/internal/audio/media/47",
            "content://media/internal/audio/media/85\njava.lang.SecurityException: secret",
        )) {
            val calls = mutableListOf<String>()
            val result = TargetRingtoneDefaults().execute(eligible) { command, _ ->
                calls += command
                if (command == "settings get system ringtone") TargetRingtoneDefaults.CommandResult.Completed(0, value, outputComplete = true)
                else response(command)
            }
            assertEquals(TargetRingtoneDefaults.Result.Uncertain(TargetRingtoneDefaults.Stage.VERIFY,
                TargetRingtoneDefaults.Reason.INVALID_OUTPUT), result)
            assertEquals(2, calls.size)
            assertFalse(calls.any { it.contains("/tmp/bad") })
        }
    }

    @Test fun concurrentCallsShareOneScanAttempt() {
        val operation = TargetRingtoneDefaults()
        val entered = CountDownLatch(1)
        val release = CountDownLatch(1)
        val scans = AtomicInteger()
        var first: TargetRingtoneDefaults.Result? = null
        val worker = thread {
            first = operation.execute(eligible) { command, _ ->
                if (command.startsWith("content call")) {
                    scans.incrementAndGet(); entered.countDown()
                    check(release.await(5, TimeUnit.SECONDS))
                }
                response(command)
            }
        }
        try {
            assertTrue(entered.await(5, TimeUnit.SECONDS))
            assertEquals(TargetRingtoneDefaults.Result.AlreadyAttempted,
                operation.execute(eligible) { _, _ -> fail("duplicate command"); error("unreachable") })
        } finally { release.countDown(); worker.join(5000) }
        assertEquals(TargetRingtoneDefaults.Result.ObservedMatchingDefaults, first)
        assertEquals(1, scans.get())
    }
}
