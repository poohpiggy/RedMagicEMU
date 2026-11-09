package app.aemu.core

import org.junit.Assert.*
import org.junit.Test

class ExperimentalVibrationBootTest {
    @Test fun disabledAndMissingAdapterRemoveStaleDeclaration() {
        for ((enabled, adapter) in listOf(false to true, true to false)) {
            val calls = mutableListOf<Boolean>()
            assertFalse(prepareExperimentalVibration(enabled, adapter) { calls += it; false })
            assertEquals(listOf(false), calls)
        }
    }

    @Test fun preparedOnlyWhenDeclarationIsReady() {
        val calls = mutableListOf<Boolean>()
        assertTrue(prepareExperimentalVibration(true, true) { calls += it; true })
        assertEquals(listOf(true), calls)
    }

    @Test fun declarationFailureRollsBack() {
        val calls = mutableListOf<Boolean>()
        val error = IllegalStateException("test")
        try {
            prepareExperimentalVibration(true, true) {
                calls += it
                if (it) throw error
                false
            }
            fail("failure must propagate")
        } catch (caught: IllegalStateException) { assertSame(error, caught) }
        assertEquals(listOf(true, false), calls)
    }

    @Test fun incompleteDeclarationRollsBack() {
        val calls = mutableListOf<Boolean>()
        assertFalse(prepareExperimentalVibration(true, true) { calls += it; false })
        assertEquals(listOf(true, false), calls)
    }
}
