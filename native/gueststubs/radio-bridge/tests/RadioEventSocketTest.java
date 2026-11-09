package app.aemu.stub;

import android.hardware.radio.messaging.IRadioMessagingIndication;
import android.hardware.radio.messaging.IRadioMessagingResponse;
import android.hardware.radio.voice.Call;
import android.hardware.radio.voice.IRadioVoiceIndication;
import android.hardware.radio.voice.IRadioVoiceResponse;
import android.net.LocalSocket;
import android.net.LocalSocketAddress;
import android.os.IBinder;
import android.os.RemoteException;
import java.io.InputStream;
import java.io.OutputStream;
import java.lang.reflect.Proxy;
import java.util.Arrays;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

/** Guest side of the isolated abstract-socket test; never registers a radio service. */
public final class RadioEventSocketTest {
    private static IBinder peer() {
        return (IBinder) Proxy.newProxyInstance(IBinder.class.getClassLoader(), new Class<?>[]{IBinder.class},
                (p, m, a) -> {
                    switch (m.getName()) {
                        case "linkToDeath": return null;
                        case "unlinkToDeath": return true;
                        case "isBinderAlive": return true;
                        case "hashCode": return System.identityHashCode(p);
                        case "equals": return p == a[0];
                        case "toString": return "synthetic-peer";
                        default: throw new UnsupportedOperationException(m.getName());
                    }
                });
    }

    private static void put64(byte[] bytes, int offset, long value) {
        for (int i = 7; i >= 0; --i) { bytes[offset + i] = (byte) value; value >>>= 8; }
    }

    private static byte[] expectedAck(long epoch, long sequence, int status) {
        byte[] expected = new byte[24];
        expected[0] = 'R'; expected[1] = 'M'; expected[2] = 'E'; expected[3] = '1';
        put64(expected, 4, epoch); put64(expected, 12, sequence);
        expected[20] = 3; expected[22] = 1; expected[23] = (byte) status;
        return expected;
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 4 || !("normal".equals(args[3]) || "partial".equals(args[3])))
            throw new IllegalArgumentException("abstract-name epoch host-pid normal|partial required");
        long epoch = Long.parseUnsignedLong(args[1]);
        int hostPid = Integer.parseInt(args[2]);
        if (epoch == 0 || hostPid <= 0 || args[0].isEmpty()) throw new IllegalArgumentException("invalid test identity");
        boolean partial = "partial".equals(args[3]);
        AtomicInteger messages = new AtomicInteger(), states = new AtomicInteger(), rings = new AtomicInteger();
        AtomicReference<Call[]> currentCalls = new AtomicReference<>();
        IBinder vr = peer(), vi = peer(), sr = peer(), si = peer();
        IRadioVoiceResponse voiceResponse = (IRadioVoiceResponse) Proxy.newProxyInstance(
                IRadioVoiceResponse.class.getClassLoader(), new Class<?>[]{IRadioVoiceResponse.class},
                (p, m, a) -> {
                    if (m.getName().equals("asBinder")) return vr;
                    if (m.getName().equals("getCurrentCallsResponse")) currentCalls.set((Call[]) a[1]);
                    return null;
                });
        IRadioVoiceIndication voiceIndication = (IRadioVoiceIndication) Proxy.newProxyInstance(
                IRadioVoiceIndication.class.getClassLoader(), new Class<?>[]{IRadioVoiceIndication.class},
                (p, m, a) -> {
                    if (m.getName().equals("asBinder")) return vi;
                    if (m.getName().equals("callStateChanged")) states.incrementAndGet();
                    if (m.getName().equals("callRing")) {
                        rings.incrementAndGet();
                        if (partial) throw new RemoteException("synthetic partial notification");
                    }
                    return null;
                });
        IRadioMessagingResponse smsResponse = (IRadioMessagingResponse) Proxy.newProxyInstance(
                IRadioMessagingResponse.class.getClassLoader(), new Class<?>[]{IRadioMessagingResponse.class},
                (p, m, a) -> m.getName().equals("asBinder") ? sr : null);
        IRadioMessagingIndication smsIndication = (IRadioMessagingIndication) Proxy.newProxyInstance(
                IRadioMessagingIndication.class.getClassLoader(), new Class<?>[]{IRadioMessagingIndication.class},
                (p, m, a) -> {
                    if (m.getName().equals("asBinder")) return si;
                    if (m.getName().equals("newSms")) {
                        if (!Arrays.equals((byte[]) a[1], new byte[]{0, 4, 11}))
                            throw new AssertionError("SMS PDU changed in transit");
                        messages.incrementAndGet();
                    }
                    return null;
                });
        RadioVoiceService voice = new RadioVoiceService("slot1", 8, 2500);
        RadioMessagingService sms = RadioMessagingService.withoutStateForTests("slot1");
        try (LocalSocket socket = new LocalSocket(LocalSocket.SOCKET_SEQPACKET)) {
            voice.setResponseFunctions(voiceResponse, voiceIndication);
            sms.setResponseFunctions(smsResponse, smsIndication);
            RadioEventSession session = new RadioEventSession(epoch, RadioEventEndpoint.withoutStateForTests(voice, sms));
            socket.connect(new LocalSocketAddress(args[0], LocalSocketAddress.Namespace.ABSTRACT));
            socket.setSoTimeout(10000);
            if (socket.getPeerCredentials().getPid() != hostPid) throw new SecurityException("unexpected host PID");
            InputStream input = socket.getInputStream(); OutputStream output = socket.getOutputStream();
            int[] expected = {1, partial ? 3 : 1, 2, 3};
            byte[] last = null;
            for (int i = 0; i < 5; ++i) {
                // 280 bytes makes any packet larger than the 279-byte protocol limit rejectable.
                byte[] buffer = new byte[280]; int count = input.read(buffer);
                if (count <= 0) throw new IllegalStateException("peer closed before all test frames");
                byte[] request = Arrays.copyOf(buffer, count);
                byte[] ack = session.receive(request);
                if (i < 4) {
                    if (!Arrays.equals(ack, expectedAck(epoch, i + 1, expected[i])))
                        throw new AssertionError("unexpected test ACK at sequence " + (i + 1));
                    if (i == 1) {
                        voice.getCurrentCalls(819);
                        Call[] calls = currentCalls.get();
                        if (calls == null || calls.length != 1 || calls[0].state != Call.STATE_INCOMING ||
                                !"+10086".equals(calls[0].number))
                            throw new AssertionError("incoming number or state changed in transit");
                    }
                    output.write(ack); output.flush();
                    if (i == 3) last = request;
                } else if (!Arrays.equals(request, last) || ack != null) {
                    throw new AssertionError("duplicate was not rejected without ACK");
                }
            }
            if (messages.get() != 1 || states.get() != 1 || rings.get() != 1)
                throw new AssertionError("duplicate or missing model callback");
            System.out.println("RADIO_EVENT_SOCKET_OK mode=" + args[3] + " sms=1 state=1 ring=1");
        } finally { voice.close(); }
    }
}
