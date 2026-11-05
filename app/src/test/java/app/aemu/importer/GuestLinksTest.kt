package app.aemu.importer

import org.junit.Assert.*
import org.junit.Test
import java.io.IOException

class GuestLinksTest {
    @Test fun convertsAbsoluteTargetsWithoutChangingGuestMeaning() {
        assertEquals("../../system/bin/sh", GuestLinks.hostTarget("vendor/bin/sh", "/system/bin/sh"))
        GuestLinks.validate(mapOf("vendor/bin/sh" to "/system/bin/sh"))
    }
    @Test(expected = IOException::class) fun rejectsDirectEscape() {
        GuestLinks.validate(mapOf("system/a" to "../../outside"))
    }
    @Test(expected = IOException::class) fun rejectsEscapeThroughLinkChain() {
        GuestLinks.validate(mapOf("a" to "deep/dir", "b" to "a/../../../outside"))
    }
    @Test(expected = IOException::class) fun rejectsCycles() {
        GuestLinks.validate(mapOf("a" to "b", "b" to "a"))
    }
    @Test(expected = IOException::class) fun rejectsBadLinkPath() {
        GuestLinks.validate(mapOf("../a" to "b"))
    }
}
