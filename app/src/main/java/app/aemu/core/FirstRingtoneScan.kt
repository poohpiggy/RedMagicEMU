package app.aemu.core

/**
 * One first-instance attempt. The caller must create the fresh marker only for a
 * new instance and ensure its default external volume is authorized to scan.
 * The marker does not protect a user's concurrent ringtone changes. A timeout
 * does not prove that the provider has stopped its work.
 */
internal object FirstRingtoneScan {
    enum class Result { Skipped, NotReady, ObservedMatchingDefaults, ConfiguredWithUserChoices, Uncertain }

    private const val PROBE = "content query --uri content://media/external_primary/audio/media --projection _id --where \"_id=-1\""
    // Each :vm process currently runs a single GuestVm; serialize its full scan and recovery.
    private val executionLock = Any()

    fun execute(store: FirstRingtoneInitStore, api: Int, model: String, abi: String, engine: Engine,
                systemAudioIndexed: Boolean, pause: (Long) -> Boolean,
                bootId: String? = HostBootId.read(),
                run: (String, Long) -> TargetRingtoneDefaults.CommandResult): Result = synchronized(executionLock) {
        executeLocked(store, api, model, abi, engine, systemAudioIndexed, pause, bootId, run)
    }

    private fun executeLocked(store: FirstRingtoneInitStore, api: Int, model: String, abi: String, engine: Engine,
                              systemAudioIndexed: Boolean, pause: (Long) -> Boolean, bootId: String?,
                              run: (String, Long) -> TargetRingtoneDefaults.CommandResult): Result {
        if (api != 36 || model != "NX809J" || abi != "arm64-v8a" || engine != Engine.N64 || !systemAudioIndexed)
            return Result.Skipped
        val prerequisites = TargetRingtoneDefaults.Prerequisites(api, model, abi, engine,
            controlledFreshInstance = true, systemAudioIndexed = true, authorizedVolumeReady = true)
        val currentBoot = HostBootId.parse(bootId)
        val initialStatus = store.status()
        when (initialStatus) {
            FirstRingtoneInitStore.Status.IN_PROGRESS -> {
                val observed = TargetRingtoneDefaults().verifyObserved(prerequisites, run)
                val completed = when (observed) {
                    TargetRingtoneDefaults.Result.ObservedMatchingDefaults -> Result.ObservedMatchingDefaults
                    TargetRingtoneDefaults.Result.ConfiguredWithUserChoices -> Result.ConfiguredWithUserChoices
                    else -> null
                }
                if (completed != null)
                    return if (store.completeObservedIfInProgress()) completed else Result.Skipped
                if (Thread.currentThread().isInterrupted || currentBoot == null ||
                    store.prepareUncertainRetry(currentBoot) != FirstRingtoneInitStore.RetryPreparation.ELIGIBLE)
                    return Result.Uncertain
            }
            FirstRingtoneInitStore.Status.FRESH -> Unit
            else -> return Result.Skipped
        }
        // Probing is read-only; leave the instance eligible if it is not ready yet.
        var ready = false
        repeat(3) { attempt ->
            if (!ready) {
                val response = try { run(PROBE, 20_000L) } catch (e: InterruptedException) {
                    Thread.currentThread().interrupt()
                    return Result.NotReady
                } catch (_: Exception) { null }
                ready = response is TargetRingtoneDefaults.CommandResult.Completed && response.code == 0 &&
                    response.outputComplete && response.output.lineSequence().map(String::trim)
                        .filterNot { it.isEmpty() || it.startsWith("WARNING: linker:") }.toList() == listOf("No result found.")
                if (!ready && attempt < 2 && !pause(5_000L)) return Result.NotReady
            }
        }
        if (!ready) return Result.NotReady
        // Only a changed host boot can rule out work left by an earlier uncertain scan.
        val claim = if (initialStatus == FirstRingtoneInitStore.Status.FRESH) {
            val freshClaim = store.claim() ?: return Result.Skipped
            if (currentBoot != null) store.recordInitialBoot(freshClaim, currentBoot)
            freshClaim
        } else {
            if (store.claimUncertainRetry(currentBoot ?: return Result.Uncertain) != FirstRingtoneInitStore.RetryClaim.ALLOWED)
                return Result.Uncertain
            null
        }
        repeat(3) { attempt ->
            when (val result = TargetRingtoneDefaults().execute(prerequisites, run)) {
                TargetRingtoneDefaults.Result.ObservedMatchingDefaults,
                TargetRingtoneDefaults.Result.ConfiguredWithUserChoices -> {
                    val completed = if (result == TargetRingtoneDefaults.Result.ObservedMatchingDefaults)
                        Result.ObservedMatchingDefaults else Result.ConfiguredWithUserChoices
                    if (claim != null) {
                        store.complete(claim)
                        return completed
                    }
                    return if (store.completeObservedIfInProgress()) completed else Result.Skipped
                }
                is TargetRingtoneDefaults.Result.Uncertain -> {
                    // VERIFY is reached only after the scan returned completely.
                    if (result.stage != TargetRingtoneDefaults.Stage.VERIFY || attempt == 2 ||
                        Thread.currentThread().isInterrupted || !pause(5_000L))
                        return Result.Uncertain
                }
                else -> return Result.Uncertain
            }
        }
        return Result.Uncertain
    }
}
