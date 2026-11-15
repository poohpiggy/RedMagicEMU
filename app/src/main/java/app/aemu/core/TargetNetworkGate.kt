package app.aemu.core

import java.io.File
import java.io.IOException
import java.nio.file.Files
import java.nio.file.LinkOption.NOFOLLOW_LINKS
import java.nio.file.StandardCopyOption

/** The API 36 engine consumes this byte before sending guest Internet traffic. */
internal object TargetNetworkGate {
    const val GUEST_PATH = "/dev/aemu_network_gate"

    fun prepare(root: File, virtualWifi: Boolean) {
        val dev = root.toPath().resolve("dev")
        if (!Files.exists(dev, NOFOLLOW_LINKS)) Files.createDirectory(dev)
        if (!Files.isDirectory(dev, NOFOLLOW_LINKS)) throw IOException("Unsafe guest device directory")
        val gate = dev.resolve("aemu_network_gate")
        if (!virtualWifi) {
            Files.deleteIfExists(gate)
            return
        }
        setClosed(root)
    }

    fun setClosed(root: File) {
        val dev = root.toPath().resolve("dev")
        if (!Files.exists(dev, NOFOLLOW_LINKS)) Files.createDirectory(dev)
        if (!Files.isDirectory(dev, NOFOLLOW_LINKS)) throw IOException("Unsafe guest device directory")
        val gate = dev.resolve("aemu_network_gate")
        val temporary = Files.createTempFile(dev, ".aemu-network-gate-", ".tmp")
        try {
            Files.write(temporary, "0\n".toByteArray(Charsets.US_ASCII))
            Files.move(temporary, gate, StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING)
        } finally {
            Files.deleteIfExists(temporary)
        }
    }
}
