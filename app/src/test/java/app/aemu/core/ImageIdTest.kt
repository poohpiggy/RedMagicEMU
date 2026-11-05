package app.aemu.core

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class ImageIdTest {
    @Test fun acceptsGeneratedIdsAndRejectsPaths() {
        assertTrue(ImageId.valid("abc234"))
        assertFalse(ImageId.valid("../abc234"))
        assertFalse(ImageId.valid("abc/234"))
        assertFalse(ImageId.valid("ABC234"))
        assertFalse(ImageId.valid(""))
    }
}
