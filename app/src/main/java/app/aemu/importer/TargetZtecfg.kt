package app.aemu.importer

import java.io.File
import java.io.IOException
import java.nio.file.Files
import java.nio.file.LinkOption
import java.nio.file.StandardOpenOption

/** Installs a private raw configuration copy at the two paths used by variantid. */
internal object TargetZtecfg {
    private const val SIZE = 512L * 1024

    fun install(root: File, source: File, cancelled: () -> Boolean = { false }) {
        val input = source.toPath()
        if (!Files.isRegularFile(input, LinkOption.NOFOLLOW_LINKS) || Files.size(input) != SIZE)
            throw IOException("Invalid ztecfg image")
        val writer = SafeTreeWriter(root, 4, SIZE, cancelled)
        writer.directory("dev/block/by-name")
        writer.directory("dev/block/bootdevice/by-name")
        Files.newInputStream(input, StandardOpenOption.READ, LinkOption.NOFOLLOW_LINKS).use {
            writer.write("dev/block/by-name/ztecfg", SIZE, it)
        }
        if (cancelled()) throw ImportCancelledException()
        // The fixed relative link stays in the guest tree and resolves to the single private copy.
        writer.links(mapOf("dev/block/bootdevice/by-name/ztecfg" to "../../by-name/ztecfg"))
    }
}
