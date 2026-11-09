package app.aemu.core;

import java.nio.channels.FileChannel;
import java.nio.channels.FileLock;
import java.nio.file.Paths;
import java.nio.file.StandardOpenOption;

/** Separate JVM for OS-level lock and process-death verification. */
public final class InstanceRuntimeLeaseChild {
    public static void main(String[] args) throws Exception {
        try (FileChannel channel = FileChannel.open(Paths.get(args[0]), StandardOpenOption.CREATE, StandardOpenOption.WRITE);
             FileLock lock = channel.lock()) {
            System.out.println("LOCKED");
            System.out.flush();
            System.in.read();
        }
    }
}
