package app.aemu.core

import java.nio.file.Files
import org.junit.Assert.*
import org.junit.Test

class GuestLauncherSelectionTest {
    @Test fun normalLaunchKeepsExistingLauncher() {
        val dir = Files.createTempDirectory("rme-launcher-select").toFile()
        try {
            assertEquals(dir.resolve("libaemu-launch.so"), selectGuestLauncher(dir, false))
        } finally { dir.deleteRecursively() }
    }

    @Test fun experimentalLauncherMustExistAndNeverFallsBack() {
        val dir = Files.createTempDirectory("rme-launcher-select").toFile()
        try {
            dir.resolve("libaemu-launch.so").writeText("normal")
            try { selectGuestLauncher(dir, true); fail("missing experiment must fail") }
            catch (_: IllegalStateException) { }
            val experiment = dir.resolve("libaemu-vibration-launch.so")
            experiment.writeText("experiment")
            assertTrue(experiment.setExecutable(true))
            assertEquals(experiment, selectGuestLauncher(dir, true))
        } finally { dir.deleteRecursively() }
    }
}
