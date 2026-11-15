package app.aemu.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Test

class SensorBridgeStatusTest {
    @Test fun registrationReportsExpectedExceptionsAndDoesNotSwallowErrors() {
        val logs = mutableListOf<String>()
        val status = SensorBridgeStatus { logs.add(it) }
        status.registrationAttempt { false }
        status.registrationAttempt { throw SecurityException("permission denied") }
        assertThrows(AssertionError::class.java) { status.registrationAttempt { throw AssertionError("fatal") } }
        assertEquals(listOf(
            "sensors: accelerometer listener registration failed",
            "sensors: accelerometer listener registration failed: SecurityException: permission denied",
        ), logs)
    }

    @Test fun intermittentSuccessDoesNotSpamCongestionAndRecoveryLogs() {
        val logs = mutableListOf<String>()
        val status = SensorBridgeStatus { logs.add(it) }
        repeat(100) {
            status.writeFailure(true, "EAGAIN")
            status.writeSucceeded()
        }
        assertEquals(listOf("sensors: accelerometer channel congested; samples dropped"), logs)
        repeat(49) { status.writeSucceeded() }
        assertEquals(listOf(
            "sensors: accelerometer channel congested; samples dropped",
            "sensors: accelerometer write recovered",
        ), logs)
    }

    @Test fun registrationFailuresAreReportedOnlyOnChange() {
        val logs = mutableListOf<String>()
        val status = SensorBridgeStatus { logs.add(it) }
        status.registration("SensorManager unavailable")
        status.registration("SensorManager unavailable")
        status.registration("accelerometer unavailable")
        status.registration("accelerometer listener registration failed")
        status.registration("accelerometer listener registered")
        assertEquals(listOf(
            "sensors: SensorManager unavailable",
            "sensors: accelerometer unavailable",
            "sensors: accelerometer listener registration failed",
            "sensors: accelerometer listener registered",
        ), logs)
    }

    @Test fun fullPipeAndOtherWriteFailuresAreDeduplicatedUntilRecovery() {
        val logs = mutableListOf<String>()
        val status = SensorBridgeStatus { logs.add(it) }
        status.writeFailure(true, "EAGAIN")
        status.writeFailure(true, "EAGAIN")
        repeat(50) { status.writeSucceeded() }
        status.writeFailure(false, "EIO")
        status.writeFailure(false, "EIO")
        repeat(50) { status.writeSucceeded() }
        status.writeFailure(false, "EIO")
        assertEquals(listOf(
            "sensors: accelerometer channel congested; samples dropped",
            "sensors: accelerometer write recovered",
            "sensors: accelerometer write failed: EIO",
            "sensors: accelerometer write recovered",
            "sensors: accelerometer write failed: EIO",
        ), logs)
    }
}
