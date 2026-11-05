package app.aemu.core

import org.junit.Assert.*
import org.junit.Test

class SafeFramebufferPathTest {
    @Test fun constrainsFramebufferToTheSelectedInstance() {
        assertEquals(listOf("images", "abcdef", "root", "dev", "graphics", "fb0"), SafeFramebufferPath.components("abcdef", false))
        assertEquals(listOf("images", "abcdef", "recovery", "dev", "graphics", "fb0"), SafeFramebufferPath.components("abcdef", true))
        assertThrows(IllegalArgumentException::class.java) { SafeFramebufferPath.components("../bad", false) }
    }
}
