package app.aemu.stub;

import android.net.LocalSocket;
import android.net.LocalSocketAddress;
import android.system.Os;
import android.system.OsConstants;
import java.io.IOException;
import java.util.Arrays;

/** Isolated diagnostic; never registers a service or sends a real radio event. */
public final class RadioGuestTransportProbe {
    public static void main(String[] args) throws Exception {
        if (args.length != 4) throw new IllegalArgumentException("socket pid physical-uid parent|child|fd");
        int pid = Integer.parseInt(args[1]), uid = Integer.parseInt(args[2]);
        String mode = args[3];
        if (!mode.equals("parent") && !mode.equals("child") && !mode.equals("fd"))
            throw new IllegalArgumentException("unknown mode");
        try (LocalSocket socket = new LocalSocket(LocalSocket.SOCKET_SEQPACKET)) {
            socket.connect(new LocalSocketAddress(args[0], LocalSocketAddress.Namespace.ABSTRACT));
            socket.setSoTimeout(10000);
            if (socket.getPeerCredentials().getPid() != pid) throw new SecurityException("sender PID mismatch");
            RadioEventSocketTransport transport = RadioEventSocketTransport.connected(socket, pid, uid);
            socket.getOutputStream().write('R');
            if (mode.equals("parent")) {
                if (!Arrays.equals(transport.read(), new byte[]{'P'}) || transport.requiresProcessExit())
                    throw new AssertionError("legitimate packet rejected");
                transport.write(new byte[]{'A'});
                System.out.println("RADIO_TRANSPORT_GUEST_ACCEPTED");
            } else {
                try { transport.read(); throw new AssertionError("unexpected packet accepted"); }
                catch (IOException expected) {
                    if (!transport.requiresProcessExit()) throw new AssertionError("rejected socket not terminal");
                }
                // Deliberately remain alive for one bounded handshake only in this diagnostic.
                // Production must terminate the receiving process after a transport failure.
                Os.setsockoptInt(socket.getFileDescriptor(), OsConstants.SOL_SOCKET, OsConstants.SO_PASSCRED, 0);
                socket.getOutputStream().write('J');
                System.out.println("RADIO_TRANSPORT_GUEST_REJECTED mode=" + mode);
            }
            if (mode.equals("parent")) {
                if (!Arrays.equals(transport.read(), new byte[]{'D'})) throw new AssertionError("sender did not finish");
            } else {
                if (socket.getInputStream().read() != 'D') throw new AssertionError("sender did not finish");
            }
            System.out.println("RADIO_TRANSPORT_GUEST_PROBE_OK mode=" + mode);
        }
    }
}
