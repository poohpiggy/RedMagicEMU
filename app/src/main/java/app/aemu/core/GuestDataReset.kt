package app.aemu.core

import app.aemu.importer.ImportCleanup
import java.io.File
import java.io.IOException
import java.nio.channels.FileChannel
import java.nio.file.Files
import java.nio.file.LinkOption.NOFOLLOW_LINKS
import java.nio.file.NoSuchFileException
import java.nio.file.Path
import java.nio.file.StandardOpenOption.READ
import java.nio.file.attribute.BasicFileAttributes

/** The caller must hold the runtime lease and verify that no Guest processes remain. */
internal object GuestDataReset {
    private const val PENDING = ".rme-data-reset-pending"
    private val directories = listOf("data", "cache", "metadata")
    private val ownerFiles = listOf("dhd.owners", "dhd.owners.seeded")
    private val ringtoneFiles = setOf(
        FirstRingtoneInitStore.FRESH_FILE, FirstRingtoneInitStore.CLAIM_FILE,
        FirstRingtoneInitStore.DONE_FILE, FirstRingtoneInitStore.RETRY_BASELINE_FILE,
    )

    fun requireComplete(instance: File) {
        if (attributes(instance.toPath().resolve(PENDING)) != null)
            throw IOException("Guest data reset is incomplete. Retry Reset data in instance settings.")
    }

    fun clear(instance: File, syncDirectory: (Path) -> Unit = ::sync, initialize: () -> Unit) {
        val parent = instance.toPath()
        val root = parent.resolve("root")
        if (!Files.isDirectory(parent, NOFOLLOW_LINKS) || !Files.isDirectory(root, NOFOLLOW_LINKS))
            throw IOException("Invalid Guest root")
        val targets = directories.map(root::resolve).filter { target ->
            val attrs = attributes(target)
            when {
                attrs == null || attrs.isDirectory -> true
                // Preserve the firmware's /cache alias. Removing /data clears its contents.
                attrs.isSymbolicLink && target.fileName.toString() == "cache" &&
                    Files.readSymbolicLink(target) == root.fileSystem.getPath("data", "cache") -> false
                else -> throw IOException("Invalid Guest directory: ${target.fileName}")
            }
        }
        val ringtonePaths = Files.newDirectoryStream(parent).use { entries ->
            entries.filter {
                val name = it.fileName.toString()
                name in ringtoneFiles || (name.startsWith(FirstRingtoneInitStore.RETRY_FILE_PREFIX) &&
                    HostBootId.parse(name.removePrefix(FirstRingtoneInitStore.RETRY_FILE_PREFIX)) != null)
            }
        }
        val stateFiles = ownerFiles.map(root::resolve) + ringtonePaths
        for (file in stateFiles) {
            if (attributes(file)?.isRegularFile == false) throw IOException("Invalid Guest state: ${file.fileName}")
        }
        val marker = parent.resolve(PENDING)
        val markerAttrs = attributes(marker)
        if (markerAttrs == null) Files.createFile(marker)
        else if (!markerAttrs.isRegularFile || markerAttrs.size() != 0L) throw IOException("Invalid reset marker")
        syncDirectory(parent)

        targets.forEach { ImportCleanup.remove(it.toFile()) }
        stateFiles.forEach { Files.deleteIfExists(it) }
        syncDirectory(root)
        initialize()
        Files.delete(marker)
        syncDirectory(parent)
    }

    private fun attributes(path: Path): BasicFileAttributes? = try {
        Files.readAttributes(path, BasicFileAttributes::class.java, NOFOLLOW_LINKS)
    } catch (_: NoSuchFileException) {
        null
    }

    private fun sync(path: Path) = FileChannel.open(path, READ, NOFOLLOW_LINKS).use { it.force(true) }
}
