package app.aemu.core

/** Re-index only the target ROM's built-in audio, without changing the user's ringtone choices. */
internal object TargetRingtoneIndex {
    enum class Result { NOT_APPLICABLE, SCANNED, FAILED, CANCELLED }
    private enum class Attempt { NOT_APPLICABLE, SCANNED, EMPTY_BUNDLE, FAILED }

    private const val QUERY_TIMEOUT_MS = 20_000L
    private const val SCAN_TIMEOUT_MS = 60_000L
    private const val AUDIO_DIR = "/product/media/audio"
    private val entries = listOf(
        "$AUDIO_DIR/ringtones/1Alpha_5.ogg" to "is_ringtone",
        "$AUDIO_DIR/notifications/1Bounce.ogg" to "is_notification",
        "$AUDIO_DIR/alarms/1Quantum_Foam.ogg" to "is_alarm",
    )
    private val exceptionTypes = listOf("SecurityException", "IllegalStateException", "IllegalArgumentException",
        "NullPointerException", "RemoteException", "RuntimeException")

    private fun queryCommand(path: String, category: String): String =
        "content query --uri content://media/internal/audio/media --projection _data " +
            "--where \"_data='$path' AND $category=1\""

    private fun hasExplicitError(output: String): Boolean = output.lineSequence().map(String::trim)
        .any { it.contains("Exception") || it.startsWith("Error:") }

    /** [run] executes a bounded Guest shell command and returns its exit code and output. */
    fun ensure(api: Int, model: String, abi: String, engine: Engine,
               report: (String) -> Unit = {},
               run: (String, Long) -> Pair<Int, String>): Result = when (attempt(api, model, abi, engine, report, run)) {
        Attempt.NOT_APPLICABLE -> Result.NOT_APPLICABLE
        Attempt.SCANNED -> Result.SCANNED
        Attempt.EMPTY_BUNDLE, Attempt.FAILED -> Result.FAILED
    }

    private fun attempt(api: Int, model: String, abi: String, engine: Engine,
                        report: (String) -> Unit,
                        run: (String, Long) -> Pair<Int, String>): Attempt {
        if (api != 36 || model != "NX809J" || abi != "arm64-v8a" || engine != Engine.N64)
            return Attempt.NOT_APPLICABLE

        fun indexed(): Boolean {
            for ((path, category) in entries) {
                val (code, output) = run(queryCommand(path, category), QUERY_TIMEOUT_MS)
                if (code != 0 || hasExplicitError(output)) { report("$category query failed"); return false }
                if (output.lineSequence().none { it.trim() == "Row: 0 _data=$path" }) {
                    report("$category query returned no matching row"); return false
                }
            }
            return true
        }
        val (code, output) = run("content call --uri content://media --method scan_file --arg $AUDIO_DIR", SCAN_TIMEOUT_MS)
        // An empty Bundle has been observed to accompany a scan that did not populate the library.
        val stream = Regex("""android\.intent\.extra\.STREAM=(?:Supplier\{VAL_PARCELABLE@\d+\+\d+\}|content://media/internal/[^\s}\]]+)""")
        val lines = output.lineSequence().map(String::trim).toList()
        val category = when {
            hasExplicitError(output) -> "exception text"
            lines.any { it == "Result: Bundle[{}]" } -> "empty bundle"
            lines.any { it == "Result: null" } -> "null result"
            output.contains("Result: Bundle[") && stream.containsMatchIn(output) -> "stream"
            else -> "other"
        }
        // Never copy subprocess output into logs: the command may report caller or path details.
        report("scan response: $category")
        if (category == "exception text") {
            val type = exceptionTypes.firstOrNull { Regex("\\b$it\\b").containsMatchIn(output) } ?: "other"
            report("scan exception: $type")
        }
        if (code != 0) { report("scan command failed"); return Attempt.FAILED }
        if (category == "empty bundle") {
            report("scan returned no stream")
            return Attempt.EMPTY_BUNDLE
        }
        if (category != "stream") {
            report("scan returned no stream")
            return Attempt.FAILED
        }
        return if (indexed()) Attempt.SCANNED else Attempt.FAILED
    }

    /** Probe the provider read-only before issuing at most one scan; [pause] returns false on VM stop. */
    fun ensureWithRetry(api: Int, model: String, abi: String, engine: Engine,
                        pause: (Long) -> Boolean, report: (String) -> Unit,
                        run: (String, Long) -> Pair<Int, String>): Result {
        if (api != 36 || model != "NX809J" || abi != "arm64-v8a" || engine != Engine.N64)
            return Result.NOT_APPLICABLE
        repeat(3) { attempt ->
            report("provider query attempt ${attempt + 1}")
            val (path, category) = entries[0]
            val (code, output) = run(queryCommand(path, category), QUERY_TIMEOUT_MS)
            val lines = output.lineSequence().map(String::trim).toList()
            val ready = code == 0 && !hasExplicitError(output) &&
                lines.any { it == "Row: 0 _data=$path" || it == "No result found." }
            if (ready) return ensure(api, model, abi, engine, report, run)
            report("provider query not ready")
            if (attempt < 2 && !pause(5_000L)) {
                report("cancelled")
                return Result.CANCELLED
            }
        }
        report("all attempts failed")
        return Result.FAILED
    }
}
