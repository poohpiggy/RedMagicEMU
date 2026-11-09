package app.aemu.core

import java.io.File
import java.io.IOException
import java.util.Locale

/** Reads the host kernel boot identifier; it does not establish process or VM identity. */
internal object HostBootId {
    private val uuid = Regex("[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}")

    /** Accept the exact UUID, optionally followed by the single newline emitted by procfs. */
    fun parse(raw: String?): String? {
        val text = raw?.removeSuffix("\n") ?: return null
        return text.takeIf { uuid.matches(it) }?.lowercase(Locale.ROOT)
    }

    /** A missing, unreadable, or malformed identifier cannot establish a reboot. */
    fun read(file: File = File("/proc/sys/kernel/random/boot_id")): String? = try {
        file.inputStream().use { stream ->
            val bytes = ByteArray(38)
            var count = 0
            while (count < bytes.size) {
                val read = stream.read(bytes, count, bytes.size - count)
                if (read < 0) break
                if (read == 0) return null
                count += read
            }
            if (count == bytes.size) null else parse(String(bytes, 0, count, Charsets.US_ASCII))
        }
    } catch (_: IOException) {
        null
    } catch (_: SecurityException) {
        null
    }

    /** The caller must obtain both identifiers from the same trusted host. */
    fun changed(previous: String?, current: String?): Boolean? {
        val before = parse(previous) ?: return null
        val after = parse(current) ?: return null
        return before != after
    }
}
