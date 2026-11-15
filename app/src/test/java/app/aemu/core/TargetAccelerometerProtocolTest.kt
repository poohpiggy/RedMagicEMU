package app.aemu.core

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.file.Files

class TargetAccelerometerProtocolTest {
    @Test fun recordPreservesSensorTimestampAccuracyAndAxisValues() {
        val cls = runCatching { Class.forName("app.aemu.core.TargetAccelerometerProtocol") }.getOrNull()
        assertNotNull("target accelerometer record encoder is missing", cls)
        val method = cls!!.getMethod("encode", java.lang.Long.TYPE, java.lang.Integer.TYPE,
            java.lang.Float.TYPE, java.lang.Float.TYPE, java.lang.Float.TYPE)
        val actual = method.invoke(null, 1234567890123L, 2, 1.25f, -2.5f, 9.81f) as ByteArray
        val expected = ByteBuffer.allocate(32).order(ByteOrder.LITTLE_ENDIAN)
            .putInt(0x324d4541).putInt(1).putLong(1234567890123L)
            .putFloat(1.25f).putFloat(-2.5f).putFloat(9.81f).putInt(2).array()
        assertArrayEquals(expected, actual)
    }

    @Test fun motionRecordPreservesAccelerometerGyroscopeAndMagnetometerType() {
        val method = TargetAccelerometerProtocol::class.java.methods.firstOrNull {
            it.name == "encode" && it.parameterTypes.size == 6
        }
        assertNotNull("typed motion record encoder is missing", method)
        for (type in intArrayOf(1, 4, 2)) {
            val actual = method!!.invoke(null, type, 1234567890123L, 2, 1.25f, -2.5f, 9.81f) as ByteArray
            val expected = ByteBuffer.allocate(32).order(ByteOrder.LITTLE_ENDIAN)
                .putInt(0x324d4541).putInt(type).putLong(1234567890123L)
                .putFloat(1.25f).putFloat(-2.5f).putFloat(9.81f).putInt(2).array()
            assertArrayEquals("sensor type $type was not preserved", expected, actual)
        }
    }

    @Test fun delayedOrNonMonotonicEventsAreNotForwardedAfterResume() {
        val method = TargetAccelerometerProtocol::class.java.methods.firstOrNull { it.name == "shouldForward" }
        assertNotNull("session timestamp filtering is missing", method)
        assertFalse(method!!.invoke(null, 99L, 100L, 0L) as Boolean)
        assertFalse(method.invoke(null, 101L, 100L, 101L) as Boolean)
        assertTrue(method.invoke(null, 102L, 100L, 101L) as Boolean)
    }

    @Test fun naturalLandscapeUsesTheFixedSystemPortraitRotation() {
        val method = TargetAccelerometerProtocol::class.java.methods.firstOrNull { it.name == "mapToPortrait" }
        assertNotNull("natural orientation mapping is missing", method)
        assertArrayEquals(floatArrayOf(2f, -1f, 3f), method!!.invoke(null, 1f, 2f, 3f, 90) as FloatArray, 0f)
        assertArrayEquals(floatArrayOf(-2f, 1f, 3f), method.invoke(null, 1f, 2f, 3f, 270) as FloatArray, 0f)
        assertArrayEquals(floatArrayOf(1f, 2f, 3f), method.invoke(null, 1f, 2f, 3f, 0) as FloatArray, 0f)
    }

    @Test fun stateFileTransitionsBetweenActiveAndInactiveWithTheSessionTimestamp() {
        val dir = Files.createTempDirectory("aemu-accel-")
        try {
            val cls = runCatching { Class.forName("app.aemu.core.TargetAccelerometerState") }.getOrNull()
            assertNotNull("accelerometer state publisher is missing", cls)
            val publish = cls!!.getMethod("publish", java.io.File::class.java, java.lang.Boolean.TYPE, java.lang.Long.TYPE)
            val file = dir.resolve("aemu_sensor_state").toFile()
            publish.invoke(null, file, true, 1234L)
            org.junit.Assert.assertEquals("1 1234\n", file.readText())
            publish.invoke(null, file, false, 2345L)
            org.junit.Assert.assertEquals("0 2345\n", file.readText())
        } finally {
            dir.toFile().deleteRecursively()
        }
    }

    @Test fun naturalLandscapeIsDetectedRegardlessOfCurrentWindowRotation() {
        val method = TargetAccelerometerProtocol::class.java.methods.firstOrNull { it.name == "portraitRotation" }
        assertNotNull("natural orientation selection is missing", method)
        org.junit.Assert.assertEquals(270, method!!.invoke(null, 2000, 1200, 0, false))
        org.junit.Assert.assertEquals(270, method.invoke(null, 1200, 2000, 1, false))
        org.junit.Assert.assertEquals(90, method.invoke(null, 1200, 2000, 3, true))
        org.junit.Assert.assertEquals(0, method.invoke(null, 1200, 2000, 0, false))
    }

    @Test fun failClosedFallbackAcceptsOnlyTheOwnedRegularStateFile() {
        val method = TargetAccelerometerState::class.java.methods.firstOrNull { it.name == "canInvalidate" }
        assertNotNull("state ownership validation is missing", method)
        assertTrue(method!!.invoke(null, 10L, 20L, 30, 10L, 20L, 30, true, "1 123\n") as Boolean)
        assertFalse(method.invoke(null, 10L, 20L, 30, 10L, 21L, 30, true, "1 123\n") as Boolean)
        assertFalse(method.invoke(null, 10L, 20L, 30, 10L, 20L, 31, true, "1 123\n") as Boolean)
        assertFalse(method.invoke(null, 10L, 20L, 30, 10L, 20L, 30, false, "1 123\n") as Boolean)
        assertFalse(method.invoke(null, 10L, 20L, 30, 10L, 20L, 30, true, "unrelated file\n") as Boolean)
    }
}
