package app.aemu.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.nio.file.Files

class SensorChannelsTest {
    @Test fun disabledLegacyPreservesAccelerometerAndDoesNotTouchExistingVibrationPath() {
        val directory = Files.createTempDirectory("rme-sensors-")
        try {
            val vibrationPath = directory.resolve("old-vibration")
            Files.write(vibrationPath, byteArrayOf(42))
            val events = mutableListOf<String>()
            val channels = SensorChannels(false, { events.add("accelerometer-start") }, { events.add("accelerometer-stop") },
                { Files.write(vibrationPath, byteArrayOf(1)); events.add("vibration-start") },
                { Files.write(vibrationPath, byteArrayOf(0)); events.add("vibration-stop") })
            channels.start()
            channels.stop()
            assertEquals(listOf("accelerometer-start", "accelerometer-stop"), events)
            assertTrue(Files.readAllBytes(vibrationPath).contentEquals(byteArrayOf(42)))
        } finally {
            Files.deleteIfExists(directory.resolve("old-vibration"))
            Files.deleteIfExists(directory)
        }
    }

    @Test fun disabledLegacyDoesNotCreateVibrationPathAtStartOrStop() {
        val directory = Files.createTempDirectory("rme-sensors-")
        try {
            val vibrationPath = directory.resolve("missing-vibration")
            val channels = SensorChannels(false, {}, {}, { Files.createFile(vibrationPath) }, { Files.createFile(vibrationPath) })
            channels.start()
            channels.stop()
            assertFalse(Files.exists(vibrationPath))
        } finally {
            Files.deleteIfExists(directory.resolve("missing-vibration"))
            Files.deleteIfExists(directory)
        }
    }

    @Test fun explicitlyEnabledLegacyRetainsBothChannelsInOriginalOrder() {
        val events = mutableListOf<String>()
        val channels = SensorChannels(true, { events.add("accelerometer-start") }, { events.add("accelerometer-stop") },
            { events.add("vibration-start") }, { events.add("vibration-stop") })
        channels.start()
        channels.stop()
        assertEquals(listOf("accelerometer-start", "vibration-start", "accelerometer-stop", "vibration-stop"), events)
    }
}
