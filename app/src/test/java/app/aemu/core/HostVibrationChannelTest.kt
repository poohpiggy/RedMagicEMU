package app.aemu.core

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertThrows
import org.junit.Test

class HostVibrationChannelTest {
    @Test fun handshakeUsesGuestProtocolAndBigEndianEpoch() {
        assertArrayEquals(
            byteArrayOf(0x52, 0x4d, 0x45, 0x48, 1, 2, 3, 4, 5, 6, 7, 8),
            HostVibrationChannel.handshake(0x0102030405060708L),
        )
    }

    @Test fun handshakeRejectsInvalidEpochs() {
        assertThrows(IllegalArgumentException::class.java) { HostVibrationChannel.handshake(0) }
        assertThrows(IllegalArgumentException::class.java) { HostVibrationChannel.handshake(-1) }
    }
}
