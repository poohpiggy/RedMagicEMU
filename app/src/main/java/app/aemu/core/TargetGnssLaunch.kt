package app.aemu.core

import android.content.Context
import android.util.AtomicFile
import java.io.File

internal object TargetGnssLaunch {
    const val executable = "/system/bin/aemu-gnss-service"
    const val serviceName = "aemu-gnss"
    private const val asset = "engines/n64-36/aemu-gnss-service"
    private const val declaration = "vendor/etc/vintf/manifest/aemu-gnss.xml"
    private val manifest = """
        <manifest version="9.0" type="device">
            <hal format="aidl" override="true">
                <name>android.hardware.gnss</name>
                <version>4</version>
                <fqname>IGnss/default</fqname>
            </hal>
        </manifest>
    """.trimIndent().encodeToByteArray()

    fun readyFile(root: File) = File(root, "dev/.aemu-gnss-ready")

    fun prepare(context: Context, root: File): Boolean {
        clear(root)
        if (!EngineAssets.exists(context, asset)) return false
        val binary = runtimeFile(root, executable.removePrefix("/"))
        write(binary, EngineAssets.open(context, asset).use { it.readBytes() })
        check(binary.setExecutable(true)) { "cannot make GNSS service executable" }
        val fragment = runtimeFile(root, declaration)
        check(!fragment.exists()) { "GNSS declaration already exists" }
        write(fragment, manifest)
        return true
    }

    fun clear(root: File) {
        val fragment = runtimeFile(root, declaration)
        if (fragment.isFile && fragment.readBytes().contentEquals(manifest)) {
            check(fragment.delete()) { "cannot remove GNSS declaration" }
        }
        val ready = readyFile(root)
        if (ready.exists()) check(ready.delete()) { "cannot reset GNSS readiness" }
    }

    private fun runtimeFile(root: File, path: String): File {
        val file = File(root, path)
        check(file.canonicalPath.startsWith(root.canonicalPath + File.separator)) { "invalid Guest GNSS path" }
        return file
    }

    private fun write(file: File, bytes: ByteArray) {
        val atomic = AtomicFile(file)
        val stream = atomic.startWrite()
        try {
            stream.write(bytes)
            atomic.finishWrite(stream)
        } catch (failure: Exception) {
            atomic.failWrite(stream)
            throw failure
        }
    }
}
