package app.aemu.core

import java.io.File
import java.io.InputStream
import java.nio.file.Files
import java.nio.file.StandardCopyOption.ATOMIC_MOVE
import java.nio.file.StandardCopyOption.COPY_ATTRIBUTES
import java.nio.file.StandardCopyOption.REPLACE_EXISTING

/** Adds the single-rear-camera profile through Nubia's camera adapter extension. */
internal class TargetCameraModeRuntime(private val root: File, private val asset: () -> InputStream) {
    private val registration = File(root, "system_ext/etc/permissions/vendor.zte.hardware.camera.transferclient.xml")
    private val original = File(registration.parentFile, registration.name + ".rme-original")
    private val transferClient = File(root, STOCK_JAR.removePrefix("/"))
    private val adapter = File(transferClient.parentFile, "rme-camera-adapter.jar")

    fun install(): Boolean {
        restore()
        if (!registration.isFile || !transferClient.isFile || registration.length() > 64 * 1024) return false
        requireRuntimePaths()
        val stock = registration.readText()
        val fileAttribute = """file="$STOCK_JAR""""
        if (stock.indexOf(fileAttribute) < 0 || stock.indexOf(fileAttribute) != stock.lastIndexOf(fileAttribute) ||
            !stock.contains("</permissions>") || stock.contains("dependency=") || adapter.exists()) return false
        val updated = stock
            .replace(fileAttribute, """$fileAttribute dependency="$LIBRARY"""")
            .replace("</permissions>", """
                |    <library name="$LIBRARY" file="/system_ext/framework/${adapter.name}" />
                |</permissions>
            """.trimMargin().trimStart())
        val bytes = asset().use { it.readBytes() }
        require(bytes.isNotEmpty()) { "Missing Guest camera mode adapter" }
        val saved = File.createTempFile(".rme-camera-", ".xml", registration.parentFile)
        try {
            Files.copy(registration.toPath(), saved.toPath(), REPLACE_EXISTING, COPY_ATTRIBUTES)
            Files.move(saved.toPath(), original.toPath(), ATOMIC_MOVE)
            replace(adapter, bytes)
            replace(registration, updated.toByteArray(), preserve = true)
        } catch (failure: Exception) {
            runCatching { restore() }.onFailure(failure::addSuppressed)
            throw failure
        } finally {
            saved.delete()
        }
        return true
    }

    fun restore() {
        if (!original.isFile) return
        requireRuntimePaths()
        Files.deleteIfExists(adapter.toPath())
        Files.move(original.toPath(), registration.toPath(), ATOMIC_MOVE, REPLACE_EXISTING)
    }

    private fun requireRuntimePaths() {
        val directory = root.canonicalFile.toPath()
        check(listOf(registration, original, adapter).all {
            !Files.isSymbolicLink(it.toPath()) && it.canonicalFile.toPath().startsWith(directory)
        }) { "Unexpected Guest camera adapter paths" }
    }

    private fun replace(dest: File, bytes: ByteArray, preserve: Boolean = false) {
        val staged = File.createTempFile(".rme-camera-", ".tmp", dest.parentFile)
        try {
            if (preserve) Files.copy(dest.toPath(), staged.toPath(), REPLACE_EXISTING, COPY_ATTRIBUTES)
            staged.writeBytes(bytes)
            if (!preserve) staged.setReadable(true, false)
            Files.move(staged.toPath(), dest.toPath(), ATOMIC_MOVE, REPLACE_EXISTING)
        } finally {
            staged.delete()
        }
    }

    private companion object {
        const val STOCK_JAR = "/system_ext/framework/vendor.zte.hardware.camera.transferclient.jar"
        const val LIBRARY = "rme-nubia-camera-adapter"
    }
}
