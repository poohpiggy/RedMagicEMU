package app.aemu.stub;

import android.net.LocalSocket;
import android.os.ParcelFileDescriptor;
import android.system.ErrnoException;
import android.system.Os;
import android.system.OsConstants;
import android.system.StructCmsghdr;
import android.system.StructMsghdr;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.Arrays;

/**
 * Receives packets on an already-connected AF_UNIX SOCK_SEQPACKET socket.
 * The caller must authenticate the physical peer and provide its PID and UID,
 * verify the socket type, and prevent sends until connected() returns. Guest
 * SO_PEERCRED UID may be rewritten and cannot authenticate the physical UID.
 * Any failure requires termination of the entire receiving process: native recvmsg
 * can install FDs before Java converts ancillary data, including on conversion failure.
 * The caller owns the socket and must supervise this rule even for unchecked errors.
 */
final class RadioEventSocketTransport implements RadioEventSessionPump.FrameTransport {
    interface Receiver { Packet receive() throws IOException; }
    interface FdCloser { void close(int fd) throws IOException; }
    interface Writer { void write(byte[] frame) throws IOException; }
    static final class Packet {
        final byte[] bytes; final int flags; final int[] levels, types; final byte[][] data;
        Packet(byte[] bytes,int flags,int[] levels,int[] types,byte[][] data) {
            this.bytes=bytes; this.flags=flags; this.levels=levels; this.types=types; this.data=data;
        }
    }
    private final int pid,uid;
    private final Receiver receiver;
    private final FdCloser closer;
    private final Writer writer;
    private volatile boolean exitRequired;
    RadioEventSocketTransport(int pid,int uid,Receiver receiver,FdCloser closer) {
        this(pid,uid,receiver,closer,frame -> { throw new IOException("no writer"); });
    }
    RadioEventSocketTransport(int pid,int uid,Receiver receiver,FdCloser closer,Writer writer) {
        if(pid<=0 || uid<0 || receiver==null || closer==null || writer==null)
            throw new IllegalArgumentException("authenticated identity and socket required");
        this.pid=pid; this.uid=uid; this.receiver=receiver; this.closer=closer; this.writer=writer;
    }
    static RadioEventSocketTransport connected(LocalSocket socket,int expectedPid,int expectedUid) throws IOException {
        if(socket==null) throw new IllegalArgumentException("socket required");
        if(expectedPid<=0 || expectedUid<0 || !socket.isConnected() || socket.getPeerCredentials().getPid()!=expectedPid)
            throw new IOException("connected peer PID mismatch");
        try { Os.setsockoptInt(socket.getFileDescriptor(),OsConstants.SOL_SOCKET,OsConstants.SO_PASSCRED,1); }
        catch(ErrnoException e) { throw new IOException("SO_PASSCRED failed; process exit required",e); }
        Receiver receiver=() -> {
            ByteBuffer body=ByteBuffer.allocate(279);
            StructMsghdr msg=new StructMsghdr(null,new ByteBuffer[]{body},null,0);
            final int n;
            // Linux MSG_CMSG_CLOEXEC; Android's public OsConstants omits this flag.
            try { n=Os.recvmsg(socket.getFileDescriptor(),msg,0x40000000); }
            catch(ErrnoException e) { throw new IOException("recvmsg failed",e); }
            StructCmsghdr[] c=msg.msg_control;
            if(n==0 && (c==null || c.length==0)) return null;
            int count=c==null ? 0 : c.length;
            int[] levels=new int[count],types=new int[count]; byte[][] data=new byte[count][];
            for(int i=0;i<count;i++) { levels[i]=c[i].cmsg_level; types[i]=c[i].cmsg_type; data[i]=c[i].cmsg_data; }
            return new Packet(Arrays.copyOf(body.array(),n),msg.msg_flags,levels,types,data);
        };
        return new RadioEventSocketTransport(expectedPid,expectedUid,receiver,
                fd -> { try(ParcelFileDescriptor p=ParcelFileDescriptor.adoptFd(fd)) { } },
                frame -> {
                    try {
                        StructMsghdr msg=new StructMsghdr(null,new ByteBuffer[]{ByteBuffer.wrap(frame)},null,0);
                        if(Os.sendmsg(socket.getFileDescriptor(),msg,0)!=frame.length)
                            throw new IOException("incomplete packet write");
                    } catch(ErrnoException e) { throw new IOException("sendmsg failed",e); }
                });
    }
    boolean requiresProcessExit() { return exitRequired; }
    @Override public byte[] read() throws IOException {
        if(exitRequired) throw new IOException("transport is terminal; process exit required");
        try {
            Packet p=receiver.receive();
            if(p==null) return null;
            if(p.levels==null || p.types==null || p.data==null || p.levels.length!=p.types.length || p.types.length!=p.data.length)
                throw new IOException("malformed control messages");
            IOException closeFailure=null;
            for(int i=0;i<p.data.length;i++) if(p.levels[i]==1 && p.types[i]==1 && p.data[i]!=null) {
                ByteBuffer rights=ByteBuffer.wrap(p.data[i]).order(ByteOrder.nativeOrder());
                while(rights.remaining()>=4) {
                    int fd=rights.getInt();
                    try { closer.close(fd); } catch(IOException e) { if(closeFailure==null) closeFailure=e; else closeFailure.addSuppressed(e); }
                }
                if(rights.hasRemaining() && closeFailure==null) closeFailure=new IOException("malformed SCM_RIGHTS");
            }
            if(closeFailure!=null) throw closeFailure;
            if(p.bytes==null || p.bytes.length<1 || p.bytes.length>279 ||
                    !RadioGuestPeerCredentials.matches(pid,uid,p.flags,p.levels,p.types,p.data))
                throw new IOException("event credentials or packet rejected");
            return p.bytes;
        } catch(IOException | RuntimeException | Error failure) {
            exitRequired=true; throw failure;
        }
    }
    @Override public void write(byte[] frame) throws IOException {
        if(exitRequired) throw new IOException("transport is terminal; process exit required");
        try { writer.write(frame); }
        catch(IOException | RuntimeException | Error failure) { exitRequired=true; throw failure; }
    }
}
