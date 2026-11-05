package app.aemu.ui

import org.junit.Assert.assertEquals
import org.junit.Test

class SingleSystemStateTest {
    @Test fun loadingDoesNotShowEmptyOrReady() {
        assertEquals(SingleSystemState.LOADING, singleSystemState(0, false, false))
        assertEquals(SingleSystemState.LOADING, singleSystemState(1, false, false))
    }
    @Test fun multipleStoredInstancesAreNotSilentlySelected() {
        assertEquals(SingleSystemState.CONFLICT, singleSystemState(2, false))
    }
    @Test fun importingTakesPrecedenceOverEmptyOrReady() {
        assertEquals(SingleSystemState.IMPORTING, singleSystemState(0, true))
        assertEquals(SingleSystemState.IMPORTING, singleSystemState(1, true))
    }
    @Test fun zeroAndOneInstancesHaveDistinctStates() {
        assertEquals(SingleSystemState.EMPTY, singleSystemState(0, false))
        assertEquals(SingleSystemState.READY, singleSystemState(1, false))
    }
    @Test fun completedImportWaitsForRefreshedInstanceBeforeShowingEmpty() {
        assertEquals(SingleSystemState.LOADING, singleSystemState(0, false, true, true))
        assertEquals(SingleSystemState.READY, singleSystemState(1, false, true, true))
    }
}
