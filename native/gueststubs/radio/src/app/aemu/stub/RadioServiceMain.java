package app.aemu.stub;

import android.os.IBinder;
import android.net.LocalSocket;
import android.net.LocalSocketAddress;
import java.io.IOException;
import java.io.File;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.StandardOpenOption;
import java.lang.reflect.Method;
import java.nio.ByteBuffer;
import java.util.concurrent.CountDownLatch;

/** Guest-only single-slot Radio service for the target Android 16 firmware. */
public final class RadioServiceMain {
    private RadioServiceMain() {}

    // Keep the endpoint and the registered service instances alive together for this process.
    private static volatile ServiceGroup activeServices;

    static final class ServiceGroup {
        final IBinder[] services;
        final RadioEventEndpoint endpoint;
        final RadioNetworkService network;
        final RadioDataService data;

        ServiceGroup(IBinder[] services, RadioEventEndpoint endpoint, RadioNetworkService network,
                RadioDataService data) {
            this.services=services;
            this.endpoint=endpoint;
            this.network=network;
            this.data=data;
        }
    }

    static ServiceGroup createServiceGroup(SyntheticSingleSlot model, String imei) {
        return createServiceGroup(model, imei, false);
    }

    static ServiceGroup createServiceGroup(SyntheticSingleSlot model, String imei, boolean lteData) {
        if (model == null) throw new IllegalArgumentException("synthetic slot required");
        RadioNetworkService network=new RadioNetworkService("slot1",model);
        RadioDataService data=new RadioDataService("slot1",model,lteData);
        RadioMessagingService messaging=new RadioMessagingService("slot1",model);
        RadioVoiceService voice=new RadioVoiceService("slot1",64,5000,model);
        RadioEventEndpoint endpoint=new RadioEventEndpoint(model,voice,messaging,network);
        IBinder[] services=new IBinder[]{
            new RadioConfigService("slot1",model).asBinder(),
            new RadioModemService("slot1",model,imei).asBinder(),
            new RadioSimService("slot1",model).asBinder(),
            network.asBinder(),
            data.asBinder(),
            messaging.asBinder(),
            voice.asBinder()
        };
        return new ServiceGroup(services,endpoint,network,data);
    }

    /** Reads only the synthetic identity prepared in this Guest's private tree. */
    static String readImei(File file) throws IOException {
        byte[] data=new byte[16];
        int count=0;
        try (InputStream in=Files.newInputStream(file.toPath(), StandardOpenOption.READ, LinkOption.NOFOLLOW_LINKS)) {
            while (count<data.length) {
                int n=in.read(data,count,data.length-count);
                if (n<0) break;
                if (n==0) throw new IOException("cannot read virtual IMEI");
                count+=n;
            }
        }
        if (count!=15) throw new IOException("invalid virtual IMEI");
        for (int i=0;i<count;i++) if (data[i]<'0' || data[i]>'9') throw new IOException("invalid virtual IMEI");
        String imei=new String(data,0,count,StandardCharsets.US_ASCII);
        if (!RadioModemService.validImei(imei)) throw new IOException("invalid virtual IMEI");
        return imei;
    }

    static boolean validEventArguments(String name, int pid, int uid, long epoch) {
        return name != null && name.matches("[A-Za-z0-9_.-]{1,80}") && pid > 0 && uid >= 0 && epoch > 0;
    }

    static RadioEventSessionPump.Result runEventTransport(RadioEventEndpoint endpoint,
            RadioEventSessionPump.FrameTransport transport, long epoch) throws IOException {
        RadioEventSessionManager manager=new RadioEventSessionManager(endpoint);
        RadioEventSession session=manager.openOnce(epoch);
        try {
            byte[] hello=ByteBuffer.allocate(24).put((byte)'R').put((byte)'M').put((byte)'E').put((byte)'1')
                    .putLong(epoch).putLong(Long.MAX_VALUE).put((byte)3).putShort((short)1).put((byte)1).array();
            transport.write(hello);
        } catch (IOException | RuntimeException | Error failure) {
            try { session.closeSession(); }
            catch (RuntimeException | Error cleanup) { if (cleanup != failure) failure.addSuppressed(cleanup); }
            throw failure;
        }
        return RadioEventSessionPump.run(session,transport);
    }

    private static final class ServiceManagerBackend implements RadioSlot1Registry.Backend {
        private final Method declared, check, add;
        ServiceManagerBackend() throws Exception {
            Class<?> manager=Class.forName("android.os.ServiceManager");
            declared=manager.getMethod("isDeclared",String.class);
            check=manager.getMethod("checkService",String.class);
            add=manager.getMethod("addService",String.class,IBinder.class);
        }
        @Override public boolean isDeclared(String name) throws Exception { return (Boolean)declared.invoke(null,name); }
        @Override public boolean isOccupied(String name) throws Exception { return check.invoke(null,name)!=null; }
        @Override public void add(String name, IBinder binder) throws Exception { add.invoke(null,name,binder); }
        @Override public boolean isPublished(String name, IBinder expected) throws Exception {
            IBinder actual=(IBinder)check.invoke(null,name);
            return actual!=null && actual.isBinderAlive() &&
                    expected.getInterfaceDescriptor().equals(actual.getInterfaceDescriptor());
        }
    }

    public static void main(String[] args) {
        try {
            if (args.length < 1 || !"--experimental-slot1".equals(args[0]))
                throw new IllegalArgumentException("missing --experimental-slot1");
            int at=1;
            boolean lteData=false;
            if (at<args.length && "--lte-data".equals(args[at])) { lteData=true; at++; }
            String eventName=null; int hostPid=0, hostUid=0; long epoch=0;
            if (at<args.length) {
                if (args.length-at != 5 || !"--event-socket".equals(args[at]))
                    throw new IllegalArgumentException("invalid event connection");
                eventName=args[at+1];hostPid=Integer.parseInt(args[at+2]);hostUid=Integer.parseInt(args[at+3]);epoch=Long.parseLong(args[at+4]);
                if (!validEventArguments(eventName,hostPid,hostUid,epoch)) throw new IllegalArgumentException("invalid event connection");
                at+=5;
            }
            if (at != args.length) throw new IllegalArgumentException("unexpected radio arguments");
            SyntheticSingleSlot model=new SyntheticSingleSlot(lteData);
            ServiceGroup group=createServiceGroup(model,readImei(new File("/data/vendor/radio/aemu-imei")),lteData);
            activeServices=group;
            Runtime.getRuntime().addShutdownHook(new Thread(() -> {
                try { group.data.close(); }
                finally { group.network.close(); }
            }, "radio-network-close"));
            RadioSlot1Registry registry=new RadioSlot1Registry(new ServiceManagerBackend(),group.services);
            registry.start();
            System.out.println("RADIO_SLOT1_NAMES_OBSERVED");
            System.out.flush();
            if (eventName != null) {
                try (LocalSocket socket=new LocalSocket(LocalSocket.SOCKET_SEQPACKET)) {
                    socket.connect(new LocalSocketAddress(eventName,LocalSocketAddress.Namespace.ABSTRACT));
                    RadioEventSocketTransport transport=RadioEventSocketTransport.connected(socket,hostPid,hostUid);
                    System.out.println("RADIO_EVENT_CONNECTED");System.out.flush();
                    RadioEventSessionPump.Result result=runEventTransport(group.endpoint,transport,epoch);
                    System.err.println("RADIO_EVENT_ENDED " + (result.isClean() ? "EOF" : "FAILED"));
                }
                // An event connection is one-shot: EOF and all failures retire this service process.
                System.exit(1);
                return;
            }
            // Keep published binders alive; a separate Guest lifecycle supervisor must stop this process.
            new CountDownLatch(1).await();
        } catch (Throwable failure) {
            System.err.println("RADIO_SLOT1_START_FAILED " + failure.getClass().getSimpleName());
            System.exit(1);
        }
    }
}
