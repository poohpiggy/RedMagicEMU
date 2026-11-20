package app.aemu.core

import java.io.File
import java.io.InputStream
import java.nio.file.Files
import java.nio.file.StandardCopyOption.ATOMIC_MOVE
import java.nio.file.StandardCopyOption.REPLACE_EXISTING
import java.security.MessageDigest

/** The NX809J runtime copy's public Vulkan and OpenCL libraries. */
internal class TargetGpuRuntime(
    private val root: File,
    private val state: File,
    private val asset: (String) -> InputStream,
) {
    companion object {
        const val INDEX = "rme-gpu/guest.idx"
        private const val OPENCL = "libOpenCL.so"
        private const val VULKAN = "libvulkan.so"
        private val libraries = listOf(VULKAN, "libRme_gles1_enc.so", "libRme_gles2_enc.so",
            "libRmeCodecCommon.so", "libc++_shared.so", OPENCL)
        private val publicLists = mapOf(
            "system-public" to "system/etc/public.libraries.txt",
            "vendor-public" to "vendor/etc/public.libraries.txt",
        )
    }

    private val active = File(state, "active")
    private val destinations = libraries.associateWith { "system/lib64/$it" } + publicLists

    fun prepare(index: String, vulkan: Boolean = true, openCl: Boolean = true) {
        if (!vulkan && !openCl) {
            restore()
            return
        }
        val entries = index.lineSequence().filter(String::isNotBlank).associate { line ->
            val (name, sha, length) = line.split('\t')
            name to (sha to length.toLong())
        }
        require(entries.keys == libraries.toSet()) { "incomplete Guest GPU bundle" }
        val selected = entries.filterKeys { if (it == OPENCL) openCl else vulkan }
        val configuration = if (vulkan && openCl) index else "vulkan=$vulkan openCl=$openCl\n$index"
        val wasActive = active.exists()
        if (wasActive && active.readText() == configuration &&
            selected.all { (name, entry) -> guestFile(name).length() == entry.second } &&
            (!openCl || publicLists.keys.all { guestFile(it).isFile })) return

        state.mkdirs()
        val legacy = File(root, "system/lib64/libvulkan.rme-original.so")
        for (name in destinations.keys) {
            val saved = File(state, name)
            val missing = File(state, "$name.absent")
            if (saved.exists() || missing.exists()) continue
            val source = if (name == VULKAN && legacy.isFile) legacy else guestFile(name)
            if (source.isFile) atomicWrite(saved) { candidate -> source.copyTo(candidate, overwrite = true) }
            else missing.createNewFile()
        }
        atomicWrite(active) { it.writeText("installing") }
        try {
            if (wasActive) {
                (entries.keys - selected.keys).forEach(::restoreFile)
                if (!openCl) publicLists.keys.forEach(::restoreFile)
            }
            for ((name, entry) in selected) {
                val (sha, size) = entry
                atomicWrite(guestFile(name)) { file ->
                    val digest = MessageDigest.getInstance("SHA-256")
                    asset("rme-gpu/$name").use { input ->
                        java.security.DigestInputStream(input, digest).use { checked ->
                            file.outputStream().use { checked.copyTo(it) }
                        }
                    }
                    val actual = digest.digest().joinToString("") { "%02x".format(it) }
                    check(file.length() == size && actual == sha) { "Guest GPU asset mismatch for $name" }
                }
            }
            if (openCl) for (name in publicLists.keys) {
                val original = File(state, name)
                val lines = if (original.exists()) original.readLines() else emptyList()
                val kept = lines.filter { it.trim().substringBefore(' ') != OPENCL }
                val updated = if (name == "system-public") kept + OPENCL else kept
                atomicWrite(guestFile(name)) { it.writeText(updated.joinToString("\n", postfix = "\n")) }
            }
            atomicWrite(active) { it.writeText(configuration) }
        } catch (failure: Exception) {
            runCatching { restore() }.onFailure(failure::addSuppressed)
            throw failure
        }
    }

    fun restore() {
        if (!active.exists()) return
        destinations.keys.forEach(::restoreFile)
        check(active.delete()) { "could not reset Guest GPU runtime state" }
    }

    private fun restoreFile(name: String) {
        val saved = File(state, name)
        val file = guestFile(name)
        if (saved.isFile) atomicWrite(file) { saved.copyTo(it, overwrite = true) }
        else if (file.exists()) check(file.delete()) { "could not remove $file" }
    }

    private fun guestFile(name: String) = File(root, destinations.getValue(name))

    private fun atomicWrite(file: File, write: (File) -> Unit) {
        val candidate = File.createTempFile(".rme-gpu-", ".tmp", file.parentFile)
        try {
            write(candidate)
            Files.move(candidate.toPath(), file.toPath(), ATOMIC_MOVE, REPLACE_EXISTING)
        } finally {
            candidate.delete()
        }
    }
}
