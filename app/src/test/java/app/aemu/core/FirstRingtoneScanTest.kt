package app.aemu.core

import java.io.IOException
import java.nio.file.FileAlreadyExistsException
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger
import org.junit.Assert.*
import org.junit.Test

class FirstRingtoneScanTest {
    private class StateFiles : FirstRingtoneInitStore.Io {
        val files = mutableMapOf<String, ByteArray>()
        override fun verifyIdentity() = Unit
        override fun exists(name: String) = synchronized(files) { files.containsKey(name) }
        override fun readAndSync(name: String) = synchronized(files) { files[name]?.copyOf() ?: throw IOException("missing") }
        override fun createAndSync(name: String, bytes: ByteArray) = synchronized(files) {
            if (files.containsKey(name)) throw FileAlreadyExistsException(name)
            files[name] = bytes.copyOf()
        }
        override fun syncDirectory() = Unit
        override fun close() = Unit
    }

    private fun store(files: StateFiles = StateFiles()) = FirstRingtoneInitStore("abcdef", files)
    private val probe = "content query --uri content://media/external_primary/audio/media --projection _id --where \"_id=-1\""
    private val scan = "content call --uri content://media --method scan_volume --arg external_primary"
    private val answers = mapOf(
        probe to "No result found.", scan to "Result: Bundle[{}]",
        "settings get system ringtone" to "content://media/internal/audio/media/85",
        "content query --uri 'content://media/internal/audio/media/85' --projection _data --where \"is_ringtone=1\"" to "Row: 0 _data=/product/media/audio/ringtones/1Alpha_5.ogg",
        "settings get system ringtone_set" to "1",
        "settings get system notification_sound" to "content://media/internal/audio/media/47",
        "content query --uri 'content://media/internal/audio/media/47' --projection _data --where \"is_notification=1\"" to "Row: 0 _data=/product/media/audio/notifications/1Bounce.ogg",
        "settings get system notification_sound_set" to "1",
        "settings get system alarm_alert" to "content://media/internal/audio/media/5",
        "content query --uri 'content://media/internal/audio/media/5' --projection _data --where \"is_alarm=1\"" to "Row: 0 _data=/product/media/audio/alarms/1Quantum_Foam.ogg",
        "settings get system alarm_alert_set" to "1",
    )

    private val boot1 = "123e4567-e89b-42d3-a456-426614174000"
    private val boot2 = "123e4567-e89b-42d3-a456-426614174001"
    private val boot3 = "123e4567-e89b-42d3-a456-426614174002"

    private fun run(store: FirstRingtoneInitStore, indexed: Boolean = true, pause: (Long) -> Boolean = { true },
                    bootId: String? = boot1,
                    command: (String, Long) -> TargetRingtoneDefaults.CommandResult) =
        FirstRingtoneScan.execute(store, 36, "NX809J", "arm64-v8a", Engine.N64, indexed, pause, bootId, command)

    @Test fun newInstanceClaimsBeforeScanningAndVerifiesAllDefaultsOnlyOnce() {
        val s = store().apply { createFresh() }
        val commands = mutableListOf<String>()
        val result = run(s) { command, _ ->
            assertEquals(if (command == probe) FirstRingtoneInitStore.Status.FRESH else FirstRingtoneInitStore.Status.IN_PROGRESS, s.status())
            commands += command
            TargetRingtoneDefaults.CommandResult.Completed(0, answers.getValue(command), true)
        }
        assertEquals(FirstRingtoneScan.Result.ObservedMatchingDefaults, result)
        assertEquals(answers.keys.toList(), commands)
        // Completion records the observed values, not provider quiescence.
        assertEquals(FirstRingtoneInitStore.Status.COMPLETE, s.status())
        assertEquals(FirstRingtoneScan.Result.Skipped, run(s) { _, _ -> fail("repeat"); error("unreachable") })
    }

    @Test fun untrackedAndDamagedInstancesNeverRunACommand() {
        val files = StateFiles()
        val s = store(files)
        assertEquals(FirstRingtoneScan.Result.Skipped, run(s) { _, _ -> fail("untracked"); error("unreachable") })
        s.createFresh()
        files.files[FirstRingtoneInitStore.FRESH_FILE] = "damaged".toByteArray()
        assertEquals(FirstRingtoneScan.Result.Skipped, run(s) { _, _ -> fail("damaged"); error("unreachable") })
    }

    @Test fun indexMustBeReadyBeforeClaim() {
        val s = store().apply { createFresh() }
        assertEquals(FirstRingtoneScan.Result.Skipped, run(s, indexed = false) { _, _ -> fail("index"); error("unreachable") })
        assertEquals(FirstRingtoneInitStore.Status.FRESH, s.status())
    }

    @Test fun failedReadOnlyProbeCanRetryOnNextStart() {
        val files = StateFiles()
        val first = store(files).apply { createFresh() }
        var firstCalls = 0
        val notReady = run(first) { command, _ ->
            assertEquals(probe, command)
            firstCalls++
            TargetRingtoneDefaults.CommandResult.TimedOut
        }
        assertEquals(FirstRingtoneScan.Result.NotReady, notReady)
        assertEquals(3, firstCalls)
        assertEquals(FirstRingtoneInitStore.Status.FRESH, first.status())

        val afterRestart = store(files)
        var scans = 0
        val result = run(afterRestart) { command, _ ->
            if (command == scan) scans++
            TargetRingtoneDefaults.CommandResult.Completed(0, answers.getValue(command), true)
        }
        assertEquals(FirstRingtoneScan.Result.ObservedMatchingDefaults, result)
        assertEquals(1, scans)
        assertEquals(FirstRingtoneInitStore.Status.COMPLETE, afterRestart.status())
    }

    @Test fun probeFailureAndTimeoutRemainRetryable() {
        val bad = listOf(
            TargetRingtoneDefaults.CommandResult.TimedOut,
            TargetRingtoneDefaults.CommandResult.Completed(1, "No result found.", true),
            TargetRingtoneDefaults.CommandResult.Completed(0, "No result found.", false),
            TargetRingtoneDefaults.CommandResult.Completed(0, "Error: unknown volume", true),
        )
        for (response in bad) {
            val s = store().apply { createFresh() }
            var count = 0
            assertEquals(FirstRingtoneScan.Result.NotReady, run(s) { command, _ -> assertEquals(probe, command); count++; response })
            assertEquals(FirstRingtoneInitStore.Status.FRESH, s.status())
            assertEquals(3, count)
        }
    }

    @Test fun readOnlyProbeRetriesWithoutRepeatingTheScan() {
        val s = store().apply { createFresh() }
        var probes = 0
        var scans = 0
        val pauses = mutableListOf<Long>()
        val result = run(s, pause = { pauses += it; true }) { command, _ ->
            if (command == probe && ++probes < 3) TargetRingtoneDefaults.CommandResult.TimedOut
            else {
                if (command == scan) scans++
                TargetRingtoneDefaults.CommandResult.Completed(0, answers.getValue(command), true)
            }
        }
        assertEquals(FirstRingtoneScan.Result.ObservedMatchingDefaults, result)
        assertEquals(listOf(5000L, 5000L), pauses)
        assertEquals(1, scans)
    }

    @Test fun cancelledProbeWaitNeverScans() {
        val s = store().apply { createFresh() }
        var calls = 0
        val result = run(s, pause = { false }) { command, _ ->
            assertEquals(probe, command)
            calls++
            TargetRingtoneDefaults.CommandResult.TimedOut
        }
        assertEquals(FirstRingtoneScan.Result.NotReady, result)
        assertEquals(1, calls)
        assertEquals(FirstRingtoneInitStore.Status.FRESH, s.status())
    }

    @Test fun concurrentReadyProbesStartOnlyOneScan() {
        val files = StateFiles()
        store(files).createFresh()
        val firstProbe = CountDownLatch(1)
        val releaseProbe = CountDownLatch(1)
        val secondStarted = CountDownLatch(1)
        val secondProbe = CountDownLatch(1)
        val scans = AtomicInteger()
        val executor = Executors.newFixedThreadPool(2)
        try {
            val first = executor.submit<FirstRingtoneScan.Result> {
                run(store(files)) { command, _ ->
                    if (command == probe) {
                        firstProbe.countDown()
                        assertTrue(releaseProbe.await(10, TimeUnit.SECONDS))
                    }
                    if (command == scan) scans.incrementAndGet()
                    TargetRingtoneDefaults.CommandResult.Completed(0, answers.getValue(command), true)
                }
            }
            assertTrue(firstProbe.await(10, TimeUnit.SECONDS))
            val second = executor.submit<FirstRingtoneScan.Result> {
                secondStarted.countDown()
                run(store(files)) { command, _ ->
                    if (command == probe) secondProbe.countDown()
                    if (command == scan) scans.incrementAndGet()
                    TargetRingtoneDefaults.CommandResult.Completed(0, answers.getValue(command), true)
                }
            }
            assertTrue(secondStarted.await(10, TimeUnit.SECONDS))
            val overlapped = secondProbe.await(500, TimeUnit.MILLISECONDS)
            releaseProbe.countDown()
            val results = listOf(first.get(15, TimeUnit.SECONDS), second.get(15, TimeUnit.SECONDS))
            assertFalse("another attempt must wait for the first attempt", overlapped)
            assertEquals(1, scans.get())
            assertEquals(1, results.count { it == FirstRingtoneScan.Result.ObservedMatchingDefaults })
            assertEquals(1, results.count { it == FirstRingtoneScan.Result.Skipped })
        } finally {
            releaseProbe.countDown()
            executor.shutdownNow()
        }
    }

    @Test fun recoveryCannotVerifyWhileRetryScanIsInFlight() {
        val files = StateFiles()
        store(files).apply { createFresh(); recordInitialBoot(requireNotNull(claim()), boot1) }
        val scanStarted = CountDownLatch(1)
        val releaseScan = CountDownLatch(1)
        val secondStarted = CountDownLatch(1)
        val secondRead = CountDownLatch(1)
        val executor = Executors.newFixedThreadPool(2)
        try {
            var firstScan = false
            val first = executor.submit<FirstRingtoneScan.Result> {
                run(store(files), bootId = boot2) { command, _ ->
                    if (command == scan) {
                        firstScan = true
                        scanStarted.countDown()
                        assertTrue(releaseScan.await(10, TimeUnit.SECONDS))
                    }
                    val output = if (command == "settings get system ringtone" && !firstScan) "null" else answers.getValue(command)
                    TargetRingtoneDefaults.CommandResult.Completed(0, output, true)
                }
            }
            assertTrue(scanStarted.await(10, TimeUnit.SECONDS))
            val second = executor.submit<FirstRingtoneScan.Result> {
                secondStarted.countDown()
                run(store(files), bootId = boot2) { _, _ ->
                    secondRead.countDown()
                    TargetRingtoneDefaults.CommandResult.TimedOut
                }
            }
            assertTrue(secondStarted.await(10, TimeUnit.SECONDS))
            val overlapped = secondRead.await(500, TimeUnit.MILLISECONDS)
            releaseScan.countDown()
            assertEquals(FirstRingtoneScan.Result.ObservedMatchingDefaults, first.get(15, TimeUnit.SECONDS))
            assertEquals(FirstRingtoneScan.Result.Skipped, second.get(15, TimeUnit.SECONDS))
            assertFalse("a concurrent recovery must not verify an in-flight scan", overlapped)
        } finally {
            releaseScan.countDown()
            executor.shutdownNow()
        }
    }

    @Test fun completedScanWithTemporarilyUnavailableVerificationRetries() {
        val s = store().apply { createFresh() }
        var scans = 0
        var ringtoneReads = 0
        val delays = mutableListOf<Long>()
        val result = run(s, pause = { delays += it; true }) { command, _ ->
            if (command == scan) scans++
            if (command == "settings get system ringtone" && ++ringtoneReads == 1)
                TargetRingtoneDefaults.CommandResult.TimedOut
            else TargetRingtoneDefaults.CommandResult.Completed(0, answers.getValue(command), true)
        }
        assertEquals(FirstRingtoneScan.Result.ObservedMatchingDefaults, result)
        assertEquals(2, scans)
        assertEquals(listOf(5000L), delays)
        assertEquals(FirstRingtoneInitStore.Status.COMPLETE, s.status())
    }

    @Test fun scanFailureOrIncompleteOutputNeverRetries() {
        val failures = listOf(
            TargetRingtoneDefaults.CommandResult.TimedOut,
            TargetRingtoneDefaults.CommandResult.Completed(0, "Result: Bundle[{}]", false),
            TargetRingtoneDefaults.CommandResult.Completed(1, "Result: Bundle[{}]", true),
            TargetRingtoneDefaults.CommandResult.Completed(0, "Result: null", true),
        )
        for (failure in failures) {
            val s = store().apply { createFresh() }
            val calls = mutableListOf<String>()
            val result = run(s) { command, _ ->
                calls += command
                if (command == scan) failure
                else TargetRingtoneDefaults.CommandResult.Completed(0, answers.getValue(command), true)
            }
            assertEquals(FirstRingtoneScan.Result.Uncertain, result)
            assertEquals(1, calls.count { it == scan })
            assertEquals(FirstRingtoneScan.Result.Uncertain, run(s) { command, _ ->
                assertEquals("settings get system ringtone", command)
                TargetRingtoneDefaults.CommandResult.TimedOut
            })
        }
    }

    @Test fun repeatedVerificationFailureHasAThreeScanLimit() {
        val s = store().apply { createFresh() }
        var scans = 0
        val delays = mutableListOf<Long>()
        val result = run(s, pause = { delays += it; true }) { command, _ ->
            if (command == scan) scans++
            if (command == "settings get system ringtone") TargetRingtoneDefaults.CommandResult.TimedOut
            else TargetRingtoneDefaults.CommandResult.Completed(0, answers.getValue(command), true)
        }
        assertEquals(FirstRingtoneScan.Result.Uncertain, result)
        assertEquals(3, scans)
        assertEquals(listOf(5000L, 5000L), delays)
        assertEquals(FirstRingtoneInitStore.Status.IN_PROGRESS, s.status())
    }

    @Test fun scanExceptionOrInterruptionNeverRetries() {
        for (interrupt in listOf(false, true)) {
            val s = store().apply { createFresh() }
            var scans = 0
            try {
                val result = run(s) { command, _ ->
                    if (command == scan) {
                        scans++
                        if (interrupt) throw InterruptedException()
                        throw IOException("scan failed")
                    }
                    TargetRingtoneDefaults.CommandResult.Completed(0, answers.getValue(command), true)
                }
                assertEquals(FirstRingtoneScan.Result.Uncertain, result)
                assertEquals(1, scans)
                if (interrupt) assertTrue(Thread.currentThread().isInterrupted)
            } finally {
                Thread.interrupted()
            }
        }
    }

    @Test fun cancelledWaitAfterVerificationFailureDoesNotRescan() {
        val s = store().apply { createFresh() }
        var scans = 0
        val result = run(s, pause = { false }) { command, _ ->
            if (command == scan) scans++
            if (command == "settings get system ringtone") TargetRingtoneDefaults.CommandResult.TimedOut
            else TargetRingtoneDefaults.CommandResult.Completed(0, answers.getValue(command), true)
        }
        assertEquals(FirstRingtoneScan.Result.Uncertain, result)
        assertEquals(1, scans)
        assertEquals(FirstRingtoneInitStore.Status.IN_PROGRESS, s.status())
    }

    @Test fun interruptedVerificationDoesNotRescan() {
        val s = store().apply { createFresh() }
        var scans = 0
        try {
            val result = run(s) { command, _ ->
                if (command == scan) scans++
                if (command == "settings get system ringtone") throw InterruptedException()
                TargetRingtoneDefaults.CommandResult.Completed(0, answers.getValue(command), true)
            }
            assertEquals(FirstRingtoneScan.Result.Uncertain, result)
            assertTrue(Thread.currentThread().isInterrupted)
            assertEquals(1, scans)
        } finally {
            Thread.interrupted()
        }
    }

    @Test fun interruptedVerificationCanRecoverWithoutAnotherScan() {
        val s = store().apply { createFresh(); assertNotNull(claim()) }
        val commands = mutableListOf<String>()
        val result = run(s) { command, _ ->
            commands += command
            TargetRingtoneDefaults.CommandResult.Completed(0, answers.getValue(command), true)
        }
        assertEquals(FirstRingtoneScan.Result.ObservedMatchingDefaults, result)
        assertFalse(commands.contains(scan))
        assertFalse(commands.contains(probe))
        assertEquals(FirstRingtoneInitStore.Status.COMPLETE, s.status())
    }

    @Test fun interruptedAttemptWithDifferentOrSilentChoiceDoesNotWrite() {
        for (value in listOf("null", "content://media/internal/audio/media/123")) {
            val s = store().apply { createFresh(); assertNotNull(claim()) }
            val commands = mutableListOf<String>()
            val result = run(s) { command, _ ->
                commands += command
                val output = when {
                    command == "settings get system ringtone" -> value
                    command.contains("/123'") -> "No result found."
                    else -> answers.getValue(command)
                }
                TargetRingtoneDefaults.CommandResult.Completed(0, output, true)
            }
            assertEquals(FirstRingtoneScan.Result.Uncertain, result)
            assertFalse(commands.contains(scan))
            assertEquals(FirstRingtoneInitStore.Status.IN_PROGRESS, s.status())
        }
    }

    @Test fun unverifiedScanRetriesAfterHostReboot() {
        val s = store().apply { createFresh(); recordInitialBoot(requireNotNull(claim()), boot1) }
        var scans = 0
        val result = run(s, bootId = boot2) { command, _ ->
            if (command == scan) scans++
            val output = if (command == "settings get system ringtone" && scans == 0) "null" else answers.getValue(command)
            TargetRingtoneDefaults.CommandResult.Completed(0, output, true)
        }
        assertEquals(FirstRingtoneScan.Result.ObservedMatchingDefaults, result)
        assertEquals(1, scans)
        assertEquals(FirstRingtoneInitStore.Status.COMPLETE, s.status())
    }

    @Test fun sameHostBootNeverRepeatsAnUnverifiedScan() {
        val s = store().apply { createFresh(); recordInitialBoot(requireNotNull(claim()), boot1) }
        var scans = 0
        val result = run(s, bootId = boot1) { command, _ ->
            if (command == scan) scans++
            val output = if (command == "settings get system ringtone") "null" else answers.getValue(command)
            TargetRingtoneDefaults.CommandResult.Completed(0, output, true)
        }
        assertEquals(FirstRingtoneScan.Result.Uncertain, result)
        assertEquals(0, scans)
        assertEquals(FirstRingtoneInitStore.Status.IN_PROGRESS, s.status())
    }

    @Test fun legacyUnknownAttemptRequiresARebootAfterBaseline() {
        val s = store().apply { createFresh(); assertNotNull(claim()) }
        var scans = 0
        fun attempt(boot: String) = run(s, bootId = boot) { command, _ ->
            if (command == scan) scans++
            val output = if (command == "settings get system ringtone" && scans == 0) "null" else answers.getValue(command)
            TargetRingtoneDefaults.CommandResult.Completed(0, output, true)
        }
        assertEquals(FirstRingtoneScan.Result.Uncertain, attempt(boot2))
        assertEquals(0, scans)
        assertEquals(FirstRingtoneScan.Result.ObservedMatchingDefaults, attempt(boot3))
        assertEquals(1, scans)
    }

    @Test fun retryAfterRebootKeepsItsOpportunityWhenVolumeIsNotReady() {
        val s = store().apply { createFresh(); recordInitialBoot(requireNotNull(claim()), boot1) }
        var scans = 0
        val first = run(s, bootId = boot2) { command, _ ->
            if (command == scan) scans++
            if (command == "settings get system ringtone")
                TargetRingtoneDefaults.CommandResult.Completed(0, "null", true)
            else TargetRingtoneDefaults.CommandResult.TimedOut
        }
        assertEquals(FirstRingtoneScan.Result.NotReady, first)
        assertEquals(0, scans)
        val second = run(s, bootId = boot2) { command, _ ->
            if (command == scan) scans++
            val output = if (command == "settings get system ringtone" && scans == 0) "null" else answers.getValue(command)
            TargetRingtoneDefaults.CommandResult.Completed(0, output, true)
        }
        assertEquals(FirstRingtoneScan.Result.ObservedMatchingDefaults, second)
        assertEquals(1, scans)
    }

    @Test fun freshInstanceStillScansWhenHostBootIdentityIsUnavailable() {
        val s = store().apply { createFresh() }
        var scans = 0
        val result = run(s, bootId = null) { command, _ ->
            if (command == scan) scans++
            TargetRingtoneDefaults.CommandResult.Completed(0, answers.getValue(command), true)
        }
        assertEquals(FirstRingtoneScan.Result.ObservedMatchingDefaults, result)
        assertEquals(1, scans)
        assertEquals(FirstRingtoneInitStore.Status.COMPLETE, s.status())
    }

    @Test fun unknownFirstBootScanRequiresBaselineThenRealHostReboot() {
        val s = store().apply { createFresh() }
        var scans = 0
        fun attempt(boot: String?) = run(s, bootId = boot) { command, _ ->
            if (command == scan) scans++
            when {
                command == scan && scans == 1 -> TargetRingtoneDefaults.CommandResult.TimedOut
                command == "settings get system ringtone" && scans == 1 -> TargetRingtoneDefaults.CommandResult.Completed(0, "null", true)
                else -> TargetRingtoneDefaults.CommandResult.Completed(0, answers.getValue(command), true)
            }
        }
        assertEquals(FirstRingtoneScan.Result.Uncertain, attempt(null))
        assertEquals(1, scans)
        assertEquals(FirstRingtoneScan.Result.Uncertain, attempt(boot2))
        assertEquals(1, scans)
        assertEquals(FirstRingtoneScan.Result.ObservedMatchingDefaults, attempt(boot3))
        assertEquals(2, scans)
    }

    @Test fun firstScanCompletesWithoutRepeatingWhenUserHasConfiguredRingtone() {
        for (value in listOf("null", "content://example.provider/custom/12")) {
            val s = store().apply { createFresh() }
            val calls = mutableListOf<String>()
            val result = run(s) { command, _ ->
                calls += command
                val output = when (command) {
                    "settings get system ringtone" -> value
                    "content query --uri content://settings/system --projection name --where \"name='ringtone'\"" -> "Row: 0 name=ringtone"
                    "content query --uri content://settings/system --projection name --where \"name='notification_sound'\"" -> "Row: 0 name=notification_sound"
                    "content query --uri content://settings/system --projection name --where \"name='alarm_alert'\"" -> "Row: 0 name=alarm_alert"
                    else -> answers.getValue(command)
                }
                TargetRingtoneDefaults.CommandResult.Completed(0, output, true)
            }
            assertEquals(FirstRingtoneScan.Result.ConfiguredWithUserChoices, result)
            assertEquals(1, calls.count { it == scan })
            assertEquals(FirstRingtoneInitStore.Status.COMPLETE, s.status())
            assertEquals(FirstRingtoneScan.Result.Skipped, run(s) { _, _ -> fail("completed"); error("unreachable") })
        }
    }

    @Test fun interruptedScanWithUserChoiceCompletesReadOnlyWithoutRescanning() {
        for (value in listOf("null", "content://example.provider/custom/12")) {
            val s = store().apply { createFresh(); recordInitialBoot(requireNotNull(claim()), boot1) }
            val calls = mutableListOf<String>()
            val result = run(s, bootId = boot2) { command, _ ->
                calls += command
                val output = when (command) {
                    "settings get system ringtone" -> value
                    "content query --uri content://settings/system --projection name --where \"name='ringtone'\"" -> "Row: 0 name=ringtone"
                    "content query --uri content://settings/system --projection name --where \"name='notification_sound'\"" -> "Row: 0 name=notification_sound"
                    "content query --uri content://settings/system --projection name --where \"name='alarm_alert'\"" -> "Row: 0 name=alarm_alert"
                    else -> answers.getValue(command)
                }
                TargetRingtoneDefaults.CommandResult.Completed(0, output, true)
            }
            assertEquals(FirstRingtoneScan.Result.ConfiguredWithUserChoices, result)
            assertFalse(calls.contains(scan))
            assertFalse(calls.contains(probe))
            assertEquals(FirstRingtoneInitStore.Status.COMPLETE, s.status())
        }
    }
}
