package app.aemu.ui

import app.aemu.core.VmSettings
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Test

class NavigationKeysTest {
    @Test fun defaultAndPreviouslySavedKeysExcludeMenu() {
        assertEquals("back,home,recents", VmSettings.NAV_DEFAULT)
        assertFalse("menu" in VmSettings.NAV_ALL)
        val saved = "back,home,recents,menu".split(',').filter { it in VmSettings.NAV_ALL }
        assertEquals(listOf("back", "home", "recents"), saved)
    }
}
