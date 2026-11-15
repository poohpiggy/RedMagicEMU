package app.aemu.core

import org.junit.Assert.*
import org.junit.Test
import java.nio.ByteBuffer
import java.nio.ByteOrder

class TargetDnsUnsupportedTest {
    @Test fun targetRawDnsFailureIsBinaryNegativeErrno() {
        val response = unsupportedTargetRawDns(36, listOf("resnsend", "2147483749", "0", "AA=="))
        assertNotNull(response)
        assertEquals(4, response!!.size)
        assertEquals(-95, ByteBuffer.wrap(response).order(ByteOrder.BIG_ENDIAN).int)
    }

    @Test fun otherCommandsAndOldApiKeepExistingHandling() {
        assertNull(unsupportedTargetRawDns(35, listOf("resnsend", "101", "0", "AA==")))
        assertNull(unsupportedTargetRawDns(36, listOf("getaddrinfo", "host", "0", "0")))
        assertNull(unsupportedTargetRawDns(36, listOf("resnsend", "101", "0")))
        assertNull(unsupportedTargetRawDns(36, listOf("resnsend", "101", "0", "AA==", "extra")))
    }
}
