package app.aemu.importer
import org.junit.Assert.*
import org.junit.Test
import java.io.IOException

class TargetPartitionsTest {
    private val extents = listOf(512L to 4096L)
    private val valid = listOf("system", "vendor", "odm", "product", "system_ext", "system_dlkm", "vendor_dlkm").associate { "${it}_a" to extents }
    @Test fun selectsAllSevenPartitionsInOrder() {
        assertEquals(listOf("system", "vendor", "odm", "product", "system_ext", "system_dlkm", "vendor_dlkm"), TargetPartitions.select(valid + ("system_b" to emptyList())).keys.toList())
    }
    @Test(expected=IOException::class) fun rejectsMissingPartition() { TargetPartitions.select(valid - "vendor_dlkm_a") }
    @Test(expected=IOException::class) fun rejectsUnrecognizedNonemptyPartition() { TargetPartitions.select(valid + ("extra_a" to extents)) }
}
