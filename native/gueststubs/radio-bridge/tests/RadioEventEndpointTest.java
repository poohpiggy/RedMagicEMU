package app.aemu.stub;

import android.hardware.radio.messaging.IRadioMessagingIndication;
import android.hardware.radio.messaging.IRadioMessagingResponse;
import android.hardware.radio.voice.IRadioVoiceIndication;
import android.hardware.radio.voice.IRadioVoiceResponse;
import android.os.IBinder;
import android.os.RemoteException;

import java.lang.reflect.Proxy;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

/** Real isolated voice/messaging models, with synthetic framework callbacks. */
public final class RadioEventEndpointTest {
    private static void check(boolean value, String message) {
        if (!value) throw new AssertionError(message);
    }

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

    private static final class Fixture implements AutoCloseable {
        final RadioVoiceService voice;
        final RadioMessagingService sms = RadioMessagingService.withoutStateForTests("slot1");
        final AtomicInteger smsEvents = new AtomicInteger(), voiceStates = new AtomicInteger(), rings = new AtomicInteger();
        final AtomicReference<byte[]> lastPdu = new AtomicReference<>();
        final AtomicReference<CountDownLatch> holdVoiceState = new AtomicReference<>();
        final CountDownLatch ringObserved = new CountDownLatch(1);
        volatile boolean failSmsRemote, failSmsRuntime, failRing;
        final RadioEventEndpoint endpoint;

        Fixture() throws Exception { this(2000); }

        Fixture(long timeoutMs) throws Exception {
            voice = new RadioVoiceService("slot1", 8, timeoutMs);
            IBinder sr = peer(), si = peer(), vr = peer(), vi = peer();
            IRadioMessagingResponse smsResponse = (IRadioMessagingResponse) Proxy.newProxyInstance(
                    IRadioMessagingResponse.class.getClassLoader(), new Class<?>[]{IRadioMessagingResponse.class},
                    (p, m, a) -> m.getName().equals("asBinder") ? sr : null);
            IRadioMessagingIndication smsIndication = (IRadioMessagingIndication) Proxy.newProxyInstance(
                    IRadioMessagingIndication.class.getClassLoader(), new Class<?>[]{IRadioMessagingIndication.class},
                    (p, m, a) -> {
                        if (m.getName().equals("asBinder")) return si;
                        if (m.getName().equals("newSms")) {
                            smsEvents.incrementAndGet(); lastPdu.set(((byte[]) a[1]).clone());
                            if (failSmsRemote) throw new RemoteException("synthetic delivery uncertainty");
                            if (failSmsRuntime) throw new IllegalStateException("synthetic callback uncertainty");
                        }
                        return null;
                    });
            IRadioVoiceResponse voiceResponse = (IRadioVoiceResponse) Proxy.newProxyInstance(
                    IRadioVoiceResponse.class.getClassLoader(), new Class<?>[]{IRadioVoiceResponse.class},
                    (p, m, a) -> m.getName().equals("asBinder") ? vr : null);
            IRadioVoiceIndication voiceIndication = (IRadioVoiceIndication) Proxy.newProxyInstance(
                    IRadioVoiceIndication.class.getClassLoader(), new Class<?>[]{IRadioVoiceIndication.class},
                    (p, m, a) -> {
                        if (m.getName().equals("asBinder")) return vi;
                        if (m.getName().equals("callStateChanged")) {
                            voiceStates.incrementAndGet();
                            CountDownLatch gate = holdVoiceState.get();
                            if (gate != null) gate.await();
                        }
                        if (m.getName().equals("callRing")) {
                            rings.incrementAndGet(); ringObserved.countDown();
                            if (failRing) throw new RemoteException("synthetic ring uncertainty");
                        }
                        return null;
                    });
            sms.setResponseFunctions(smsResponse, smsIndication);
            voice.setResponseFunctions(voiceResponse, voiceIndication);
            endpoint = RadioEventEndpoint.withoutStateForTests(voice, sms);
        }

        @Override public void close() { voice.close(); }
    }

    private static byte[] ascii(String value) { return value.getBytes(StandardCharsets.US_ASCII); }

    private static void invalidInput() throws Exception {
        try (Fixture f = new Fixture()) {
            check(f.endpoint.dispatch(0, new byte[]{1}) == RadioEventEndpoint.Outcome.REJECTED, "unknown kind accepted");
            check(f.endpoint.dispatch(1, null) == RadioEventEndpoint.Outcome.REJECTED, "null PDU accepted");
            check(f.endpoint.dispatch(1, new byte[0]) == RadioEventEndpoint.Outcome.REJECTED, "empty PDU accepted");
            check(f.endpoint.dispatch(1, new byte[257]) == RadioEventEndpoint.Outcome.REJECTED, "oversized PDU accepted");
            check(f.endpoint.dispatch(2, null) == RadioEventEndpoint.Outcome.REJECTED, "null number accepted");
            check(f.endpoint.dispatch(2, new byte[]{(byte) 0xff}) == RadioEventEndpoint.Outcome.REJECTED, "non-ASCII number accepted");
            check(f.endpoint.dispatch(2, ascii("12a")) == RadioEventEndpoint.Outcome.REJECTED, "non-numeric number accepted");
            check(f.endpoint.dispatch(2, ascii("+")) == RadioEventEndpoint.Outcome.REJECTED, "empty international number accepted");
            check(f.endpoint.dispatch(2, ascii("123456789012345678901234567890123")) == RadioEventEndpoint.Outcome.REJECTED,
                    "oversized number accepted");
            check(f.smsEvents.get() == 0 && f.voiceStates.get() == 0 && f.rings.get() == 0,
                    "invalid input reached a model callback");
        }
    }

    private static void submittedAndBusy() throws Exception {
        try (Fixture f = new Fixture()) {
            byte[] pdu = {0, (byte) 0x80, (byte) 0xff};
            check(f.endpoint.dispatch(1, pdu) == RadioEventEndpoint.Outcome.SUBMITTED, "valid SMS not submitted");
            check(f.smsEvents.get() == 1 && Arrays.equals(f.lastPdu.get(), pdu), "SMS indication incorrect");
            check(f.endpoint.dispatch(1, new byte[]{7}) != RadioEventEndpoint.Outcome.SUBMITTED,
                    "SMS awaiting framework acknowledgement reported a second submission");
            check(f.smsEvents.get() == 1, "pending SMS was automatically repeated");
            check(f.endpoint.dispatch(2, ascii("+10086")) == RadioEventEndpoint.Outcome.SUBMITTED,
                    "valid incoming call not submitted");
            check(f.voiceStates.get() == 1 && f.rings.get() == 1, "incoming call callbacks missing");
            check(f.endpoint.dispatch(2, ascii("+10086")) == RadioEventEndpoint.Outcome.REJECTED,
                    "second call while busy was not rejected");
            check(f.voiceStates.get() == 1 && f.rings.get() == 1, "busy call caused another indication");
        }
    }

    private static void acceptedBoundaries() throws Exception {
        try (Fixture f = new Fixture()) {
            byte[] pdu = new byte[256]; pdu[255] = (byte) 0xff;
            check(f.endpoint.dispatch(1, pdu) == RadioEventEndpoint.Outcome.SUBMITTED,
                    "maximum length PDU was not submitted");
            check(f.smsEvents.get() == 1 && Arrays.equals(f.lastPdu.get(), pdu), "boundary PDU changed in transit");
            check(f.endpoint.dispatch(2, ascii("+12345678901234567890123456789012")) == RadioEventEndpoint.Outcome.SUBMITTED,
                    "valid 32-digit international number rejected");
            check(f.voiceStates.get() == 1 && f.rings.get() == 1, "boundary call callbacks missing");
        }
    }

    private static void uncertainVoice() throws Exception {
        try (Fixture f = new Fixture()) {
            f.failRing = true;
            check(f.endpoint.dispatch(2, ascii("10086")) == RadioEventEndpoint.Outcome.UNCERTAIN,
                    "partially delivered call reported success or rejection");
            check(f.voiceStates.get() == 1 && f.rings.get() == 1, "partially delivered call was retried");
            check(f.endpoint.dispatch(2, ascii("10086")) == RadioEventEndpoint.Outcome.REJECTED,
                    "existing call after uncertain delivery was treated as a new submission");
            check(f.voiceStates.get() == 1 && f.rings.get() == 1, "uncertain call was re-injected");
        }
    }

    private static void uncertainSms(boolean runtime) throws Exception {
        try (Fixture f = new Fixture()) {
            f.failSmsRemote = !runtime; f.failSmsRuntime = runtime;
            check(f.endpoint.dispatch(1, new byte[]{1, 2}) == RadioEventEndpoint.Outcome.UNCERTAIN,
                    "SMS callback failure was not reported as uncertain");
            check(f.smsEvents.get() == 1, "SMS callback was retried after an uncertain result");
            check(f.endpoint.dispatch(1, new byte[]{3}) != RadioEventEndpoint.Outcome.SUBMITTED,
                    "pending ambiguous SMS was treated as submitted");
            check(f.smsEvents.get() == 1, "ambiguous SMS state triggered another callback");
        }
    }

    private static void timeoutIsUncertain() throws Exception {
        CountDownLatch release = new CountDownLatch(1);
        try (Fixture f = new Fixture(500)) {
            f.holdVoiceState.set(release);
            try {
                check(f.endpoint.dispatch(2, ascii("10086")) == RadioEventEndpoint.Outcome.UNCERTAIN,
                        "timed-out callback was reported as submitted or rejected");
                check(f.voiceStates.get() == 1 && f.rings.get() == 0,
                        "timeout did not happen while the callback was in flight");
            } finally { release.countDown(); }
            check(f.ringObserved.await(2, TimeUnit.SECONDS), "in-flight callback did not finish after release");
            check(f.endpoint.dispatch(2, ascii("10086")) == RadioEventEndpoint.Outcome.REJECTED,
                    "timed-out call was submitted twice");
            check(f.voiceStates.get() == 1 && f.rings.get() == 1, "timeout triggered another call indication");
        } finally { release.countDown(); }
    }

    public static void main(String[] args) throws Exception {
        invalidInput(); submittedAndBusy(); acceptedBoundaries(); uncertainVoice(); uncertainSms(false); uncertainSms(true);
        timeoutIsUncertain();
        System.out.println("RADIO_EVENT_ENDPOINT_OK");
    }
}
