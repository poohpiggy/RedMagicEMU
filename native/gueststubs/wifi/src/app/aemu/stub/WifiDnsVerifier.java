package app.aemu.stub;

import android.content.Context;
import android.net.ConnectivityManager;
import android.net.LocalSocket;
import android.net.LocalSocketAddress;
import android.os.Handler;
import android.os.Looper;
import java.lang.reflect.Method;
import java.util.concurrent.atomic.AtomicReference;

/** Guest-only authenticated observer. Does not register or change any network. */
public final class WifiDnsVerifier {
    private WifiDnsVerifier() { }
    public static void main(String[] args) {
        if (args.length != 5 || !"--dns-verifier".equals(args[0])) { System.exit(2); return; }
        try {
            final String name=args[1];
            final int hostPid=Integer.parseInt(args[2]), hostUid=Integer.parseInt(args[3]);
            final long epoch=Long.parseLong(args[4]);
            if (!name.matches("[A-Za-z0-9_.-]{1,80}") || hostPid<=0 || hostUid<0 || epoch<=0)
                throw new IllegalArgumentException("invalid channel arguments");
            Looper.prepare();
            final AtomicReference<WifiDnsNetworkSource> source=new AtomicReference<>();
            final WifiDnsSession session=new WifiDnsSession(epoch, new WifiDnsGuard.Source() {
                @Override public WifiDnsGuard.State read() {
                    WifiDnsNetworkSource current=source.get();
                    return current == null ? null : current.read();
                }
                @Override public boolean report(boolean available) {
                    WifiDnsNetworkSource current=source.get();
                    return current != null && current.report(available);
                }
            });
            final LocalSocket socket=new LocalSocket(LocalSocket.SOCKET_SEQPACKET);
            socket.connect(new LocalSocketAddress(name, LocalSocketAddress.Namespace.ABSTRACT));
            final WifiDnsTransport transport=WifiDnsTransport.connected(socket,hostPid,hostUid);
            transport.write(WifiDnsWire.ack(epoch,Long.MAX_VALUE,true));
            Thread worker=new Thread(() -> {
                try {
                    for (;;) {
                        byte[] frame=transport.read();
                        if (frame==null) throw new IllegalStateException("channel closed");
                        byte[] result=session.receive(frame);
                        if (result==null) throw new IllegalStateException("invalid channel frame");
                        transport.write(result);
                    }
                } catch (Throwable failure) {
                    System.err.println("WIFI_DNS_VERIFIER_ENDED " + failure.getClass().getName());
                    System.err.flush();
                    System.exit(1);
                }
            }, "wifi-dns-verifier");
            worker.setDaemon(true); worker.start();
            Method get=Class.forName("android.os.SystemProperties").getMethod("get",String.class);
            int attempts=0;
            while (attempts++<600 && !"1".equals(get.invoke(null,"sys.boot_completed"))) Thread.sleep(500);
            if (!"1".equals(get.invoke(null,"sys.boot_completed"))) throw new IllegalStateException("guest boot incomplete");
            Context context=WifiSystemContext.obtain();
            ConnectivityManager cm=context.getSystemService(ConnectivityManager.class);
            source.set(new WifiDnsNetworkSource(cm,new Handler(Looper.myLooper())));
            System.out.println("WIFI_DNS_VERIFIER_READY"); System.out.flush();
            Looper.loop();
            throw new IllegalStateException("guest looper stopped");
        } catch (Throwable failure) {
            System.err.println("WIFI_DNS_VERIFIER_FAILED " + failure.getClass().getName());
            System.err.flush(); System.exit(1);
        }
    }
}
