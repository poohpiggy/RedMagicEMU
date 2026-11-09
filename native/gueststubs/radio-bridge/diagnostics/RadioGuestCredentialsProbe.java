package app.aemu.stub;

import android.net.LocalSocket;
import android.net.LocalSocketAddress;
import android.system.Os;
import android.system.OsConstants;
import android.system.StructCmsghdr;
import android.system.StructMsghdr;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;

/** Standalone diagnostic only: does not access a radio service or personal data. */
public final class RadioGuestCredentialsProbe {
    private static int receive(LocalSocket socket, int expectedUid, int expectedPid, byte expectedByte,
            boolean shouldMatch)
            throws Exception {
        ByteBuffer body = ByteBuffer.allocate(1);
        StructMsghdr msg = new StructMsghdr(null, new ByteBuffer[]{body}, null, 0);
        int n = Os.recvmsg(socket.getFileDescriptor(), msg, 0);
        if (n != 1 || body.array()[0] != expectedByte || msg.msg_flags != 0 || msg.msg_control == null
                || msg.msg_control.length != 1) throw new AssertionError("bad packet or ancillary data");
        StructCmsghdr c = msg.msg_control[0];
        // Linux SCM_CREDENTIALS is 2; Android's public OsConstants does not expose it.
        if (c.cmsg_level != OsConstants.SOL_SOCKET || c.cmsg_type != 2 || c.cmsg_data.length != 12)
            throw new AssertionError("SCM_CREDENTIALS missing or invalid");
        ByteBuffer credentials = ByteBuffer.wrap(c.cmsg_data).order(ByteOrder.nativeOrder());
        int pid = credentials.getInt(), uid = credentials.getInt(), gid = credentials.getInt();
        System.out.println("packet=" + (char) expectedByte + " pid=" + pid + " uid=" + uid + " gid=" + gid);
        if (uid != expectedUid || pid <= 0)
            throw new AssertionError("unexpected sender credentials");
        boolean matched = RadioGuestPeerCredentials.matches(expectedPid, expectedUid, msg.msg_flags,
                new int[]{c.cmsg_level}, new int[]{c.cmsg_type}, new byte[][]{c.cmsg_data});
        if (matched != shouldMatch) throw new AssertionError("per-frame credential validator mismatch");
        System.out.println("validator=" + (matched ? "accepted" : "rejected"));
        return pid;
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 3) throw new IllegalArgumentException("socket host-pid host-uid");
        int hostPid = Integer.parseInt(args[1]), hostUid = Integer.parseInt(args[2]);
        try (LocalSocket socket = new LocalSocket(LocalSocket.SOCKET_SEQPACKET)) {
            socket.connect(new LocalSocketAddress(args[0], LocalSocketAddress.Namespace.ABSTRACT));
            Os.setsockoptInt(socket.getFileDescriptor(), OsConstants.SOL_SOCKET, OsConstants.SO_PASSCRED, 1);
            socket.setSoTimeout(10000);
            if (socket.getPeerCredentials().getPid() != hostPid) throw new AssertionError("host peer mismatch");
            socket.getOutputStream().write('R');
            receive(socket, hostUid, hostPid, (byte) 'P', true);
            int childPid = receive(socket, hostUid, hostPid, (byte) 'C', false);
            if (childPid == hostPid) throw new AssertionError("fork sender impersonated parent");
            socket.getOutputStream().write(ByteBuffer.allocate(4).order(ByteOrder.BIG_ENDIAN).putInt(childPid).array());
            System.out.println("RADIO_GUEST_CREDENTIALS_PROBE_OK");
        }
    }
}
