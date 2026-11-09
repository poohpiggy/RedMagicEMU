package app.aemu.core

import java.util.concurrent.atomic.AtomicBoolean

/**
 * One in-memory attempt. The caller must actually
 * establish and maintain each prerequisite throughout the operation, and persist
 * exclusive, one-shot ownership across processes and restarts. In particular,
 * authorizedVolumeReady means the caller has authorized scanning the whole
 * external_primary volume, not merely confirmed it exists. The runner must
 * explicitly detect its own timeout.
 * ObservedMatchingDefaults covers the command and observed settings only; it does not prove
 * persistence, cache contents, or that provider-side asynchronous work stopped.
 * An uncertain result does not prove the provider stopped writing either.
 */
internal class TargetRingtoneDefaults {
    data class Prerequisites(
        val api: Int,
        val model: String,
        val abi: String,
        val engine: Engine,
        val controlledFreshInstance: Boolean,
        val systemAudioIndexed: Boolean,
        val authorizedVolumeReady: Boolean,
    )

    sealed class CommandResult {
        /** outputComplete must mean EOF was observed and no output was truncated. */
        data class Completed(val code: Int, val output: String, val outputComplete: Boolean) : CommandResult()
        data object TimedOut : CommandResult()
    }

    enum class Stage { SCAN, VERIFY }
    enum class Reason { TIMEOUT, EXCEPTION, INVALID_OUTPUT, OUTPUT_INCOMPLETE, COMMAND_FAILED }
    sealed class Result {
        data object NotEligible : Result()
        data object AlreadyAttempted : Result()
        data object ObservedMatchingDefaults : Result()
        /** The OEM flags and setting rows exist, but at least one value differs from the defaults. */
        data object ConfiguredWithUserChoices : Result()
        data class Uncertain(val stage: Stage, val reason: Reason) : Result()
    }

    private val attempted = AtomicBoolean(false)

    private data class Expected(val setting: String, val category: String, val path: String)

    private val expected = listOf(
        Expected("ringtone", "is_ringtone", "/product/media/audio/ringtones/1Alpha_5.ogg"),
        Expected("notification_sound", "is_notification", "/product/media/audio/notifications/1Bounce.ogg"),
        Expected("alarm_alert", "is_alarm", "/product/media/audio/alarms/1Quantum_Foam.ogg"),
    )
    private val safeUri = Regex("""content://media/internal/audio/media/[1-9][0-9]{0,18}(?:\?[A-Za-z0-9._~%=&+\-]+)?""")
    private val otherContentUri = Regex("""content://[A-Za-z0-9][A-Za-z0-9._~%/?=&+:\-]*""")
    private val internalMediaPath = "content://media/internal/audio/media"
    private fun isInternalMediaUri(uri: String): Boolean = uri == internalMediaPath ||
        uri.startsWith("$internalMediaPath/") || uri.startsWith("$internalMediaPath?")

    private sealed class Read {
        data class Value(val text: String) : Read()
        data class Failure(val result: Result.Uncertain) : Read()
    }

    /** Only reads Guest state; matching values do not prove the provider has stopped. */
    fun verifyObserved(prerequisites: Prerequisites, run: (String, Long) -> CommandResult): Result {
        if (!targetEligible(prerequisites)) return Result.NotEligible
        return verifyContents(run)
    }

    private fun targetEligible(p: Prerequisites): Boolean = p.api == 36 && p.model == "NX809J" &&
        p.abi == "arm64-v8a" && p.engine == Engine.N64 && p.controlledFreshInstance && p.systemAudioIndexed

    private fun read(stage: Stage, command: String, timeout: Long, run: (String, Long) -> CommandResult): Read {
        val result = try { run(command, timeout) }
        catch (e: InterruptedException) {
            Thread.currentThread().interrupt()
            return Read.Failure(Result.Uncertain(stage, Reason.EXCEPTION))
        } catch (_: Exception) {
            return Read.Failure(Result.Uncertain(stage, Reason.EXCEPTION))
        }
        return when (result) {
            CommandResult.TimedOut -> Read.Failure(Result.Uncertain(stage, Reason.TIMEOUT))
            is CommandResult.Completed -> {
                if (result.code != 0) Read.Failure(Result.Uncertain(stage, Reason.COMMAND_FAILED))
                else if (!result.outputComplete) Read.Failure(Result.Uncertain(stage, Reason.OUTPUT_INCOMPLETE))
                else payload(result.output)?.let { Read.Value(it) }
                    ?: Read.Failure(Result.Uncertain(stage, Reason.INVALID_OUTPUT))
            }
        }
    }

    fun execute(prerequisites: Prerequisites, run: (String, Long) -> CommandResult): Result {
        if (!targetEligible(prerequisites) || !prerequisites.authorizedVolumeReady) return Result.NotEligible
        if (!attempted.compareAndSet(false, true)) return Result.AlreadyAttempted

        val scan = read(Stage.SCAN,
            "content call --uri content://media --method scan_volume --arg external_primary", 60_000, run)
        if (scan is Read.Failure) return scan.result
        if ((scan as Read.Value).text != "Result: Bundle[{}]") return Result.Uncertain(Stage.SCAN, Reason.INVALID_OUTPUT)
        return verifyContents(run)
    }

    private fun verifyContents(run: (String, Long) -> CommandResult): Result {
        for (entry in expected) {
            val value = read(Stage.VERIFY, "settings get system ${entry.setting}", 20_000, run)
            if (value is Read.Failure) return value.result
            val uri = (value as Read.Value).text
            if (uri.length > 1024) return Result.Uncertain(Stage.VERIFY, Reason.INVALID_OUTPUT)
            if (!safeUri.matches(uri)) {
                if (isInternalMediaUri(uri)) return Result.Uncertain(Stage.VERIFY, Reason.INVALID_OUTPUT)
                if (uri == "null" || otherContentUri.matches(uri)) return verifyConfigured(run)
                return Result.Uncertain(Stage.VERIFY, Reason.INVALID_OUTPUT)
            }
            // The validated URI cannot contain a quote or shell metacharacters outside the quoted argument.
            val lookup = read(Stage.VERIFY,
                "content query --uri '$uri' --projection _data --where \"${entry.category}=1\"", 20_000, run)
            if (lookup is Read.Failure) return lookup.result
            val row = (lookup as Read.Value).text
            if (row != "Row: 0 _data=${entry.path}") {
                if (row.startsWith("Row: 0 _data=/")) return verifyConfigured(run)
                return Result.Uncertain(Stage.VERIFY, Reason.INVALID_OUTPUT)
            }
            val flag = read(Stage.VERIFY, "settings get system ${entry.setting}_set", 20_000, run)
            if (flag is Read.Failure) return flag.result
            if ((flag as Read.Value).text != "1") return Result.Uncertain(Stage.VERIFY, Reason.INVALID_OUTPUT)
        }
        return Result.ObservedMatchingDefaults
    }

    /** Check names only: never interpolate an observed user URI into a shell command. */
    private fun verifyConfigured(run: (String, Long) -> CommandResult): Result {
        var hasUserChoice = false
        for (entry in expected) {
            val flag = read(Stage.VERIFY, "settings get system ${entry.setting}_set", 20_000, run)
            if (flag is Read.Failure) return flag.result
            if ((flag as Read.Value).text != "1") return Result.Uncertain(Stage.VERIFY, Reason.INVALID_OUTPUT)
            val row = read(Stage.VERIFY,
                "content query --uri content://settings/system --projection name --where \"name='${entry.setting}'\"",
                20_000, run)
            if (row is Read.Failure) return row.result
            if ((row as Read.Value).text != "Row: 0 name=${entry.setting}")
                return Result.Uncertain(Stage.VERIFY, Reason.INVALID_OUTPUT)
            val value = read(Stage.VERIFY, "settings get system ${entry.setting}", 20_000, run)
            if (value is Read.Failure) return value.result
            val uri = (value as Read.Value).text
            if (uri.length > 1024) return Result.Uncertain(Stage.VERIFY, Reason.INVALID_OUTPUT)
            if (safeUri.matches(uri)) {
                val lookup = read(Stage.VERIFY,
                    "content query --uri '$uri' --projection _data --where \"${entry.category}=1\"", 20_000, run)
                if (lookup is Read.Failure) return lookup.result
                val path = (lookup as Read.Value).text
                if (!path.startsWith("Row: 0 _data=/"))
                    return Result.Uncertain(Stage.VERIFY, Reason.INVALID_OUTPUT)
                if (path != "Row: 0 _data=${entry.path}") hasUserChoice = true
            } else if (isInternalMediaUri(uri)) {
                return Result.Uncertain(Stage.VERIFY, Reason.INVALID_OUTPUT)
            } else if (uri != "null" && !otherContentUri.matches(uri)) {
                return Result.Uncertain(Stage.VERIFY, Reason.INVALID_OUTPUT)
            } else {
                hasUserChoice = true
            }
        }
        return if (hasUserChoice) Result.ConfiguredWithUserChoices else Result.ObservedMatchingDefaults
    }

    private fun payload(output: String): String? {
        if (output.length > 65_536) return null
        val lines = output.lineSequence().map(String::trim).filter(String::isNotEmpty).toList()
        if (lines.any { it.contains("Exception") || it.startsWith("Error:") }) return null
        val values = lines.filterNot { it.startsWith("WARNING: linker:") }
        return values.singleOrNull()
    }
}
