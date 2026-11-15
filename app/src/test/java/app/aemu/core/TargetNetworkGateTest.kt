package app.aemu.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.nio.file.Files

class TargetNetworkGateTest {
    @Test fun virtualWifiStartsClosedAndEthernetRemovesAGateFromAnEarlierBoot() {
        val root = Files.createTempDirectory("target-network-gate").toFile()
        try {
            val gate = root.resolve("dev/aemu_network_gate")
            gate.parentFile.mkdirs()

            TargetNetworkGate.prepare(root, virtualWifi = true)
            assertTrue(gate.isFile)
            assertEquals("0\n", gate.readText())

            gate.writeText("1\n")
            TargetNetworkGate.prepare(root, virtualWifi = false)
            assertFalse(gate.exists())
        } finally {
            root.deleteRecursively()
        }
    }
}
