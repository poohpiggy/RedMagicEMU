package app.aemu.core

import java.io.File
import java.io.InputStream

internal object PowerRequestPeer {
    fun describe(pid: Int, uid: Int, procRoot: File = File("/proc")): String {
        val proc = File(procRoot, pid.toString())
        val process = if (pid > 0) runCatching {
            proc.resolve("comm").inputStream().use { readToken(it, '\n'.code) }
                ?.takeIf { it.matches(Regex("[A-Za-z0-9_.:@+\\-]{1,64}")) }
        }.getOrNull() else null
        val executable = if (pid > 0) runCatching {
            proc.resolve("cmdline").inputStream().use { input ->
                val runner = readToken(input, 0)?.substringAfterLast('/')
                if (runner?.matches(Regex("libaemu64_[0-9]+\\.so")) != true) return@use null
                readToken(input, 0)?.takeIf { guestExecutable(it) }
            }
        }.getOrNull() else null
        return buildString {
            append("props: sys.powerctl peer pid=$pid uid=$uid")
            process?.let { append(" process=$it") }
            executable?.let { append(" guest=$it") }
        }
    }

    private fun readToken(input: InputStream, terminator: Int): String? {
        val bytes = ByteArray(512)
        for (i in bytes.indices) {
            val b = input.read()
            if (b == -1) return null
            if (b == terminator || b == 0) {
                return if (i == 0) null else String(bytes, 0, i, Charsets.UTF_8)
            }
            bytes[i] = b.toByte()
        }
        return null
    }

    private fun guestExecutable(value: String): Boolean {
        val knownProcess = value == "system_server" || value == "zygote64" || value == "zygote"
        val guestBinary = listOf("/system/bin/", "/vendor/bin/", "/apex/").any { value.startsWith(it) } &&
            value.matches(Regex("/[A-Za-z0-9_./@+\\-]{1,255}")) && value.split('/').none { it == ".." }
        return knownProcess || guestBinary
    }

    fun applyWithDiagnostics(
        name: String,
        value: String,
        peer: () -> String,
        log: (String) -> Unit,
        apply: (String, String) -> Unit,
    ) {
        if (name == "sys.powerctl") {
            try {
                val message = try { peer() } catch (_: Exception) { "props: sys.powerctl peer unavailable" }
                log(message)
            } catch (_: Exception) {
                // Diagnostics must not prevent the actual property request.
            }
        }
        apply(name, value)
    }
}
