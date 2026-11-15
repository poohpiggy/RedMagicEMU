package app.aemu.importer

import java.io.ByteArrayInputStream
import java.io.File
import java.io.IOException
import java.security.MessageDigest
import org.junit.Assert.*
import org.junit.Test

class TargetAodMaskResourceTest {
    private fun resource() = File("src/main/assets/engines/redmagic/aod_theme64_alpha.png").readBytes()
    private fun sha(bytes: ByteArray) = MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") { "%02x".format(it) }

    @Test fun projectPngDecodesGrayscaleSamplesWithVerifiedRawDigest() {
        val mask = TargetAodMaskResource.decode(ByteArrayInputStream(resource()))
        assertEquals(1216 * 2688, mask.size)
        assertEquals("019bfeea5f8e5109befa231f8febce04732320ff1e256e51fa28b092e6231c7a", sha(mask))
        assertEquals(0, mask[0].toInt())
        assertTrue(mask.any { it.toInt() and 255 == 255 })
    }

    @Test fun corruptTruncatedAndOversizeAssetsAreRejected() {
        val correct = resource()
        val altered = correct.copyOf().apply { this[100] = (this[100].toInt() xor 1).toByte() }
        for (bad in listOf(altered, correct.copyOf(correct.size - 1), correct + ByteArray(200_000))) {
            assertThrows(IOException::class.java) { TargetAodMaskResource.decode(ByteArrayInputStream(bad)) }
        }
    }
}
