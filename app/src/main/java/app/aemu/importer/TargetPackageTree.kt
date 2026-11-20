package app.aemu.importer

import java.io.File
import java.io.IOException
import java.nio.channels.FileChannel
import java.nio.file.StandardOpenOption

/** Build the Guest working tree from the verified firmware snapshot. */
internal object TargetPackageTree {
    fun extract(superFile: File, ramdiskFile: File, root: File, cancelled: () -> Boolean): List<BootImage.CpioEntry> =
        FileChannel.open(superFile.toPath(), StandardOpenOption.READ).use { channel ->
            extract(StrictChannelSource(channel), ramdiskFile, root, cancelled)
        }

    fun extract(source: RandomSource, ramdiskFile: File, root: File, cancelled: () -> Boolean): List<BootImage.CpioEntry> {
        val parts = TargetPartitions.select(SuperImage.partitions(source))
        val ramdisk = TargetRamdisk.parse(ramdiskFile.readBytes(), cancelled = cancelled)
        val output = TargetTree(root, cancelled)
        for ((name, extents) in parts) {
            if (cancelled()) throw ImportCancelledException()
            val partition = ExtentSource(source, extents, strict = true)
            if (!ErofsReader.probe(partition)) throw IOException("Invalid $name partition image")
            output.partition(name, ErofsImage(ReadAheadSource(partition), cancelled))
        }
        output.finish(ramdisk)
        return ramdisk
    }
}


