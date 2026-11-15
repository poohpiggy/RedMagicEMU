package app.aemu.core

import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.io.File
import java.nio.file.Files
import java.nio.file.StandardCopyOption

/** AEM2 records for the NX809J/API 36 64-bit sensor HAL. */
internal object TargetAccelerometerProtocol {
    const val RECORD_SIZE = 32
    private const val MAGIC = 0x324d4541

    @JvmStatic fun encode(timestampNanos: Long, accuracy: Int, x: Float, y: Float, z: Float): ByteArray =
        encode(1, timestampNanos, accuracy, x, y, z)

    @JvmStatic fun encode(type: Int, timestampNanos: Long, accuracy: Int, x: Float, y: Float, z: Float): ByteArray =
        ByteBuffer.allocate(RECORD_SIZE).order(ByteOrder.LITTLE_ENDIAN)
            .putInt(MAGIC).putInt(type).putLong(timestampNanos)
            .putFloat(x).putFloat(y).putFloat(z).putInt(accuracy).array()

    @JvmStatic fun shouldForward(timestampNanos: Long, sessionStartNanos: Long, previousNanos: Long): Boolean =
        timestampNanos >= sessionStartNanos && timestampNanos > previousNanos

    /** Fixed natural-frame transform; this is not the current window rotation. */
    @JvmStatic fun mapToPortrait(x: Float, y: Float, z: Float, rotation: Int): FloatArray = when (rotation) {
        90 -> floatArrayOf(y, -x, z)
        270 -> floatArrayOf(-y, x, z)
        else -> floatArrayOf(x, y, z)
    }

    /** Android DisplayRotation: default portrait for a naturally landscape display. */
    @JvmStatic fun portraitRotation(width: Int, height: Int, displayRotation: Int, reverseDefault: Boolean): Int {
        val landscapeNatural = if (displayRotation == 1 || displayRotation == 3) height > width else width > height
        return if (!landscapeNatural) 0 else if (reverseDefault) 90 else 270
    }
}

/** Publish a whole state generation with a same-directory atomic rename. */
internal object TargetAccelerometerState {
    /** Only a current bridge-owned, singly linked regular state record may be invalidated in place. */
    @JvmStatic fun canInvalidate(ownedDevice: Long, ownedInode: Long, ownedUid: Int,
                                 device: Long, inode: Long, uid: Int, regular: Boolean, content: String): Boolean =
        regular && ownedDevice == device && ownedInode == inode && ownedUid == uid &&
            content.matches(Regex("[01] [0-9]+\\n"))

    @JvmStatic fun publish(file: File, active: Boolean, earliestNanos: Long) {
        val dir = file.parentFile ?: throw IllegalArgumentException("state file has no parent")
        if (!dir.isDirectory && !dir.mkdirs()) throw java.io.IOException("state directory unavailable")
        val tmp = Files.createTempFile(dir.toPath(), ".aemu-sensor-", ".tmp")
        try {
            Files.write(tmp, "${if (active) 1 else 0} $earliestNanos\n".toByteArray(Charsets.US_ASCII))
            if (!tmp.toFile().setReadable(true, false)) throw java.io.IOException("state file cannot be read by guest")
            Files.move(tmp, file.toPath(), StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING)
        } finally {
            Files.deleteIfExists(tmp)
        }
    }
}
