package app.aemu.importer

import java.io.File
import java.io.IOException
import java.nio.channels.FileChannel
import java.nio.file.StandardOpenOption

/** Build the seven verified target partitions into a private tree. Caller must authenticate the package first. */
internal object TargetPackageTree {
    fun extract(superFile: File, ramdiskFile: File, root: File, cancelled: () -> Boolean): List<BootImage.CpioEntry> {
        FileChannel.open(superFile.toPath(), StandardOpenOption.READ).use { channel ->
            val source = StrictChannelSource(channel)
            val parts = TargetPartitions.select(SuperImage.partitions(source))
            val ramdisk = TargetRamdisk.parse(ramdiskFile.readBytes(), cancelled = cancelled)
            val output = TargetTree(root, cancelled)
            for ((name, ranges) in parts) {
                if (cancelled()) throw ImportCancelledException()
                val part = ExtentSource(source, ranges, strict = true)
                if (!ErofsReader.probe(part)) throw IOException("Target partition $name is not EROFS")
                output.partition(name, ErofsImage(part, cancelled))
            }
            output.finish(ramdisk)
            return ramdisk
        }
    }
}


