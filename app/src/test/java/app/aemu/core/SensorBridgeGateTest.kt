package app.aemu.core

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class SensorBridgeGateTest {
    @Test fun bridgeOnlyRunsWhenEnabledVisibleAndReady() {
        val gate = SensorBridgeGate(true)
        assertTrue(gate.canListen(running = true, visible = true, channelReady = true, stateReady = true))
        gate.enabled = false
        assertFalse(gate.canListen(true, true, true, true))
        gate.enabled = true
        assertFalse(gate.canListen(false, true, true, true))
        assertFalse(gate.canListen(true, false, true, true))
        assertFalse(gate.canListen(true, true, false, true))
        assertFalse(gate.canListen(true, true, true, false))
        assertTrue(gate.canListen(true, true, true, true))
    }
}
