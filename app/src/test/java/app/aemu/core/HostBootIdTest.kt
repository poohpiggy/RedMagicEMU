package app.aemu.core

import java.io.File
import java.util.Locale
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class HostBootIdTest {
    @get:Rule val temp = TemporaryFolder()

    private val first = "123e4567-e89b-42d3-a456-426614174000"
    private val second = "123e4567-e89b-42d3-a456-426614174001"

    @Test fun parsesCanonicalUuidAndOneKernelNewline() {
        assertEquals(first, HostBootId.parse(first))
        assertEquals(first, HostBootId.parse("$first\n"))
        assertEquals(first, HostBootId.parse(first.uppercase(Locale.ROOT)))
    }

    @Test fun rejectsMalformedOrAmbiguousValues() {
        for (raw in listOf(null, "", "1-1-1-1-1", " $first", "$first ", "$first\n\n", "$first\r\n",
            "123e4567-e89b-42d3-a456-42661417400g", "123e4567e89b42d3a456426614174000")) {
            assertNull("unexpected valid boot id: $raw", HostBootId.parse(raw))
        }
    }

    @Test fun readsOnlyAValidBootIdFromTheRequestedFile() {
        val f = temp.newFile("boot_id")
        f.writeText("$first\n")
        assertEquals(first, HostBootId.read(f))
        f.writeText("$first\nextra")
        assertNull(HostBootId.read(f))
        assertNull(HostBootId.read(File(temp.root, "missing")))
        assertNull(HostBootId.read(temp.root))
    }

    @Test fun comparesOnlyTwoValidBootIds() {
        assertEquals(false, HostBootId.changed(first, first.uppercase(Locale.ROOT)))
        assertEquals(true, HostBootId.changed(first, second))
        assertNull(HostBootId.changed(null, second))
        assertNull(HostBootId.changed(first, "invalid"))
    }
}
