package app.aemu.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class HostLteLocationTest {
    @Test fun onlyFreshServingCellMetadataIsPublishable() {
        val cell = HostLteObservation(20_000L, "310", "026", 100, 200, 300, 765, -94)
        assertTrue(cell.isValid(23_000L))
        assertFalse(cell.isValid(50_001L))
        assertFalse(cell.isValid(19_999L))
        assertFalse(cell.copy(ci = Int.MAX_VALUE).isValid(23_000L))
        assertFalse(cell.copy(mnc = "a26").isValid(23_000L))
        assertEquals("1 20000 310 026 100 200 300 765 -94", cell.encode())
    }
}
