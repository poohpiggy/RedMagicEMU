package app.aemu.core

/**
 * In-memory prototype for coordinating first-instance ringtone initialization.
 * The caller must establish persistence, verification and writer quiescence before
 * reporting them here. This class neither performs nor verifies those operations.
 */
internal class FirstRingtoneInitState private constructor(private var phase: Phase) {
    enum class Terminal { SUCCESS, ABANDONED }

    private enum class Phase {
        EXISTING, PREPARING, INTENT_PENDING, READY, WRITING, UNCERTAIN,
        SUCCESS_PENDING, ABANDON_PENDING, SUCCESS, ABANDONED,
    }

    val mayAcceptInput: Boolean
        @Synchronized get() = phase == Phase.EXISTING || phase == Phase.SUCCESS || phase == Phase.ABANDONED

    val mayStartWrite: Boolean
        @Synchronized get() = phase == Phase.READY

    @Synchronized fun beginWrite(): FirstRingtoneInitState = transition(Phase.PREPARING, Phase.INTENT_PENDING)

    @Synchronized fun writeIntentPersisted(): FirstRingtoneInitState = transition(Phase.INTENT_PENDING, Phase.READY)

    @Synchronized fun writerStarted(): FirstRingtoneInitState = transition(Phase.READY, Phase.WRITING)

    @Synchronized fun writeVerified(): FirstRingtoneInitState = transition(Phase.WRITING, Phase.SUCCESS_PENDING)

    @Synchronized fun writeUncertain(): FirstRingtoneInitState = transition(Phase.WRITING, Phase.UNCERTAIN)

    @Synchronized fun abandonBeforeWrite(): FirstRingtoneInitState {
        check(phase == Phase.PREPARING || phase == Phase.READY) { "Cannot abandon from $phase" }
        phase = Phase.ABANDON_PENDING
        return this
    }

    @Synchronized fun intentStoreQuiescentAndAbandon(): FirstRingtoneInitState =
        transition(Phase.INTENT_PENDING, Phase.ABANDON_PENDING)

    @Synchronized fun writerQuiescentAndAbandon(): FirstRingtoneInitState =
        transition(Phase.UNCERTAIN, Phase.ABANDON_PENDING)

    @Synchronized fun successPersisted(record: Terminal): FirstRingtoneInitState {
        require(record == Terminal.SUCCESS) { "Expected success record" }
        return transition(Phase.SUCCESS_PENDING, Phase.SUCCESS)
    }

    @Synchronized fun abandonmentPersisted(record: Terminal): FirstRingtoneInitState {
        require(record == Terminal.ABANDONED) { "Expected abandonment record" }
        return transition(Phase.ABANDON_PENDING, Phase.ABANDONED)
    }

    private fun transition(expected: Phase, next: Phase): FirstRingtoneInitState {
        check(phase == expected) { "Expected $expected, was $phase" }
        phase = next
        return this
    }

    companion object {
        fun existing() = FirstRingtoneInitState(Phase.EXISTING)
        fun fresh() = FirstRingtoneInitState(Phase.PREPARING)
        fun interruptedWhileWriting() = FirstRingtoneInitState(Phase.UNCERTAIN)
    }
}
