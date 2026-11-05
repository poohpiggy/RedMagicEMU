package app.aemu.ui

import java.io.File
import java.io.FileOutputStream
import java.nio.file.Files
import java.nio.file.StandardCopyOption

/** Records only the state of the last run, never the guest log or its contents. */
internal class BootHistory(private val directory: File) {
    enum class Status { STARTING, FAILED, STOPPED }
    private val file = File(directory, "last-guest-boot")

    @Synchronized fun previous(id: String): Status? {
        if (!file.exists()) return null
        val lines = file.readLines()
        check(lines.size == 2) { "Invalid last boot record" }
        if (lines[0] != id) return null
        val status = Status.valueOf(lines[1])
        return status.takeUnless { it == Status.STOPPED }
    }

    @Synchronized fun starting(id: String) = write(id, Status.STARTING)
    @Synchronized fun failed(id: String) = write(id, Status.FAILED)
    @Synchronized fun stopped(id: String) {
        if (previous(id) != Status.FAILED) write(id, Status.STOPPED)
    }

    private fun write(id: String, status: Status) {
        require('\n' !in id && '\r' !in id)
        val temp = File.createTempFile("last-guest-boot-", ".tmp", directory)
        try {
            FileOutputStream(temp).use { out ->
                out.write("$id\n${status.name}\n".toByteArray(Charsets.UTF_8))
                out.fd.sync()
            }
            Files.move(temp.toPath(), file.toPath(), StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE)
        } finally {
            temp.delete()
        }
    }
}
