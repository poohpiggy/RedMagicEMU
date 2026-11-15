package app.aemu.core
import org.junit.Assert.*
import org.junit.Test
class TargetHostAvailabilityTest {
    @Test fun lostOldNetworkDoesNotOverrideNewValidatedDefault() {
        val old=Any(); val current=Any()
        assertTrue(targetHostAvailable(current,old,true))
        assertFalse(targetHostAvailable(current,current,true))
        assertFalse(targetHostAvailable(null,old,true))
        assertFalse(targetHostAvailable(current,null,false))
    }
}
