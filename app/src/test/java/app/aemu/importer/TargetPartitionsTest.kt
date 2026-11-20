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
    @Test fun ordersPartitionsByTheEndOfTheirLatestExtent() {
        val geometry = valid + mapOf(
            "system_a" to listOf(65536L to 8192L),
            "vendor_a" to listOf(8192L to 4096L, 32768L to 4096L),
            "odm_a" to listOf(16384L to 4096L),
        )
        assertEquals(
            listOf("product", "system_ext", "system_dlkm", "vendor_dlkm", "odm", "vendor", "system"),
            TargetPartitions.select(geometry).keys.toList(),
        )
    }
    @Test(expected=IOException::class) fun rejectsMissingPartition() { TargetPartitions.select(valid - "vendor_dlkm_a") }
    @Test(expected=IOException::class) fun rejectsUnrecognizedNonemptyPartition() { TargetPartitions.select(valid + ("extra_a" to extents)) }
}
