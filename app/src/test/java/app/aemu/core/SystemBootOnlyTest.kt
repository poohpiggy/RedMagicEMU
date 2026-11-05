package app.aemu.core

import org.junit.Test

class SystemBootOnlyTest {
    @Test(expected = IllegalStateException::class)
    fun recoveryBootIsRejected() { requireSystemBoot(true) }
    @Test fun systemBootAllowed() { requireSystemBoot(false) }
}
