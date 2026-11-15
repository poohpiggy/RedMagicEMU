package app.aemu.stub;

import android.system.ErrnoException;
import android.system.Os;
import android.system.OsConstants;
import java.io.File;
import java.io.FileDescriptor;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.channels.SeekableByteChannel;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.NoSuchFileException;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.nio.file.StandardOpenOption;
import java.nio.file.attribute.BasicFileAttributes;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;

/** Guest-owned switch and saved-network state; separate from host AP configuration. */
final class WifiPersistentState {
    interface DirectorySync { void sync(Path directory) throws IOException; }
    static final class DurabilityException extends IOException {
        DurabilityException(Exception cause) { super("Wi-Fi state directory sync failed", cause); }
    }
    static final class Snapshot {
        final boolean enabled;
        final boolean saved;
        Snapshot(boolean enabled, boolean saved) { this.enabled = enabled; this.saved = saved; }
    }

    private final Path file;
    private final String identity;
    private final DirectorySync directorySync;

    WifiPersistentState(File file, String ssid, String bssid) {
        this(file, ssid, bssid, WifiPersistentState::syncDirectory);
    }

    WifiPersistentState(File file, String ssid, String bssid, DirectorySync directorySync) {
        if (file == null || ssid == null || bssid == null) throw new IllegalArgumentException("state identity required");
        if (directorySync == null) throw new IllegalArgumentException("state directory sync required");
        this.file = file.toPath();
        this.directorySync = directorySync;
        try {
            MessageDigest hash = MessageDigest.getInstance("SHA-256");
            hash.update(ssid.getBytes(StandardCharsets.UTF_8));
            hash.update((byte) 0);
            byte[] bytes = hash.digest(bssid.getBytes(StandardCharsets.UTF_8));
            StringBuilder text = new StringBuilder(64);
            for (byte value : bytes) text.append(Character.forDigit((value >>> 4) & 15, 16))
                    .append(Character.forDigit(value & 15, 16));
            identity = text.toString();
        } catch (NoSuchAlgorithmException impossible) { throw new IllegalStateException(impossible); }
    }

    Snapshot read() throws IOException {
        Path parent = file.getParent();
        if (parent == null || !Files.isDirectory(parent, LinkOption.NOFOLLOW_LINKS))
            throw new IOException("Wi-Fi state directory unavailable");
        BasicFileAttributes attributes;
        try { attributes = Files.readAttributes(file, BasicFileAttributes.class, LinkOption.NOFOLLOW_LINKS); }
        catch (NoSuchFileException absent) { return new Snapshot(true, true); }
        if (!attributes.isRegularFile() || attributes.size() > 128)
            throw new IOException("invalid Wi-Fi state file");
        ByteBuffer input = ByteBuffer.allocate(129);
        try (SeekableByteChannel channel = Files.newByteChannel(file,
                StandardOpenOption.READ, LinkOption.NOFOLLOW_LINKS)) {
            while (input.hasRemaining()) {
                int count = channel.read(input);
                if (count < 0) break;
                if (count == 0) throw new IOException("Wi-Fi state read stalled");
            }
        }
        if (!input.hasRemaining()) throw new IOException("oversized Wi-Fi state file");
        String contents = new String(input.array(), 0, input.position(), StandardCharsets.US_ASCII);
        String[] fields = contents.split("\n", -1);
        if (fields.length != 5 || !"v1".equals(fields[0]) || !fields[1].matches("[0-9a-f]{64}")
                || !("0".equals(fields[2]) || "1".equals(fields[2]))
                || !("0".equals(fields[3]) || "1".equals(fields[3])) || !fields[4].isEmpty())
            throw new IOException("corrupt Wi-Fi state file");
        return new Snapshot("1".equals(fields[2]), !identity.equals(fields[1]) || "1".equals(fields[3]));
    }

    void write(boolean enabled, boolean saved) throws IOException {
        Path parent = file.getParent();
        if (parent == null || !Files.isDirectory(parent, LinkOption.NOFOLLOW_LINKS))
            throw new IOException("Wi-Fi state directory unavailable");
        if (Files.exists(file, LinkOption.NOFOLLOW_LINKS) && !Files.isRegularFile(file, LinkOption.NOFOLLOW_LINKS))
            throw new IOException("invalid Wi-Fi state destination");
        Path temporary = Files.createTempFile(parent, ".redmagic-wifi-state-", ".tmp");
        boolean moved = false;
        try {
            byte[] data = ("v1\n" + identity + "\n" + (enabled ? "1" : "0") + "\n"
                    + (saved ? "1" : "0") + "\n").getBytes(StandardCharsets.US_ASCII);
            try (FileOutputStream output = new FileOutputStream(temporary.toFile())) {
                output.write(data);
                output.getFD().sync();
            }
            Files.move(temporary, file, StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
            moved = true;
            try { directorySync.sync(parent); }
            catch (IOException | RuntimeException failure) { throw new DurabilityException(failure); }
        } finally { if (!moved) Files.deleteIfExists(temporary); }
    }

    private static void syncDirectory(Path directory) throws IOException {
        FileDescriptor descriptor = null;
        try {
            // O_DIRECTORY is not exposed by the public Android SDK; the target is Linux/ARM64.
            descriptor = Os.open(directory.toString(), OsConstants.O_RDONLY | 0x10000 /* O_DIRECTORY */ | OsConstants.O_CLOEXEC, 0);
            Os.fsync(descriptor);
        } catch (ErrnoException failure) {
            throw new IOException("cannot sync Wi-Fi state directory", failure);
        } finally {
            if (descriptor != null) try { Os.close(descriptor); }
            catch (ErrnoException failure) { throw new IOException("cannot close Wi-Fi state directory", failure); }
        }
    }
}
