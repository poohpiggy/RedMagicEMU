package app.aemu.importer
import java.io.IOException

/** Exact non-empty LP partitions observed in the user-designated package. */
internal object TargetPartitions {
    private val names = listOf("system", "vendor", "odm", "product", "system_ext", "system_dlkm", "vendor_dlkm")
    fun select(parts: Map<String, List<Pair<Long, Long>>>): Map<String, List<Pair<Long, Long>>> {
        val expected = names.map { "${it}_a" }.toSet()
        if (parts.filterValues { it.isNotEmpty() }.keys != expected) throw IOException("Unexpected target firmware partitions")
        return names.associateWith { parts.getValue("${it}_a") }
    }
}
