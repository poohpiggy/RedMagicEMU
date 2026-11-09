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
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

/** Isolated state/bridge contract, not a real subscribed SIM or a framework delivery test. */
public final class RadioEventSharedStateTest {
    static void check(boolean value, String reason) { if (!value) throw new AssertionError(reason); }
    /** Uses the real model; callbacks below are synthetic, not the phone framework. */
    static final class ReadyFixture extends SyntheticSingleSlot {
        ReadyFixture() { setRadioPower(true); }
    }
    static IBinder peer() {
        return (IBinder) Proxy.newProxyInstance(IBinder.class.getClassLoader(), new Class<?>[]{IBinder.class}, (p,m,a) -> {
            switch (m.getName()) {
                case "linkToDeath": return null;
                case "unlinkToDeath": return true;
                case "isBinderAlive": return true;
                case "hashCode": return System.identityHashCode(p);
                case "equals": return p == a[0];
                default: throw new UnsupportedOperationException(m.getName());
            }
        });
    }
    static final class Fixture implements AutoCloseable {
        final SyntheticSingleSlot slot;
        final RadioVoiceService voice;
        final RadioMessagingService sms;
        final RadioEventEndpoint endpoint;
        final AtomicInteger smsEvents = new AtomicInteger(), rings = new AtomicInteger(), states = new AtomicInteger();
        final AtomicReference<byte[]> deliveredPdu = new AtomicReference<>();
        volatile boolean disconnectDuringVoice, powerOffDuringSms, powerCycleDuringSms, failSmsWithIllegalState;
        Fixture(SyntheticSingleSlot model) throws Exception {
            slot = model;
            voice = new RadioVoiceService("slot1", 8, 2000, slot);
            sms = new RadioMessagingService("slot1", slot);
            IBinder vr=peer(), vi=peer(), sr=peer(), si=peer();
            IRadioVoiceResponse vresp=(IRadioVoiceResponse)Proxy.newProxyInstance(IRadioVoiceResponse.class.getClassLoader(),new Class<?>[]{IRadioVoiceResponse.class},(p,m,a)->m.getName().equals("asBinder")?vr:null);
            IRadioVoiceIndication vind=(IRadioVoiceIndication)Proxy.newProxyInstance(IRadioVoiceIndication.class.getClassLoader(),new Class<?>[]{IRadioVoiceIndication.class},(p,m,a)->{ if(m.getName().equals("asBinder"))return vi; if(m.getName().equals("callStateChanged")){states.incrementAndGet(); if(disconnectDuringVoice)slot.disconnect();} if(m.getName().equals("callRing"))rings.incrementAndGet(); return null; });
            IRadioMessagingResponse sresp=(IRadioMessagingResponse)Proxy.newProxyInstance(IRadioMessagingResponse.class.getClassLoader(),new Class<?>[]{IRadioMessagingResponse.class},(p,m,a)->m.getName().equals("asBinder")?sr:null);
            IRadioMessagingIndication sind=(IRadioMessagingIndication)Proxy.newProxyInstance(IRadioMessagingIndication.class.getClassLoader(),new Class<?>[]{IRadioMessagingIndication.class},(p,m,a)->{ if(m.getName().equals("asBinder"))return si; if(m.getName().equals("newSms")){smsEvents.incrementAndGet(); deliveredPdu.set(((byte[])a[1]).clone()); if(powerOffDuringSms || powerCycleDuringSms)slot.setRadioPower(false); if(powerCycleDuringSms)slot.setRadioPower(true); if(failSmsWithIllegalState)throw new IllegalStateException("synthetic callback failure");} return null; });
            voice.setResponseFunctions(vresp,vind); sms.setResponseFunctions(sresp,sind);
            endpoint = new RadioEventEndpoint(slot, voice, sms);
        }
        public void close() { voice.close(); }
    }
    static byte[] frame(long epoch,long seq,int kind,byte[] payload) {
        byte[] b=new byte[23+payload.length]; b[0]='R'; b[1]='M'; b[2]='E'; b[3]='1';
        for(int i=7;i>=0;i--){b[4+i]=(byte)epoch;epoch>>>=8;b[12+i]=(byte)seq;seq>>>=8;}
        b[20]=(byte)kind;b[21]=(byte)(payload.length>>>8);b[22]=(byte)payload.length;
        System.arraycopy(payload,0,b,23,payload.length);return b;
    }
    public static void main(String[] args) throws Exception {
        SyntheticSingleSlot unready = SyntheticSingleSlot.forTests();
        try (Fixture f = new Fixture(unready)) {
            byte[] pdu={0,4,11}; byte[] number="+10086".getBytes(StandardCharsets.US_ASCII);
            check(f.endpoint.dispatch(1,pdu)==RadioEventEndpoint.Outcome.REJECTED,"OFF accepted SMS");
            check(f.endpoint.dispatch(2,number)==RadioEventEndpoint.Outcome.REJECTED,"OFF accepted call");
            check(f.smsEvents.get()==0 && f.rings.get()==0,"unready event reached callback");
            try { f.voice.injectIncoming("+10086"); throw new AssertionError("direct voice bypassed state"); }
            catch (RemoteException expected) { }
            try { new RadioEventEndpoint(SyntheticSingleSlot.forTests(), f.voice, f.sms); throw new AssertionError("mismatched state accepted"); }
            catch (IllegalArgumentException expected) { }
            unready.disconnect();
            check(f.endpoint.dispatch(2,number)==RadioEventEndpoint.Outcome.REJECTED,"disconnected accepted call");
        }
        // The callbacks are local test doubles; real framework delivery is not verified.
        try (Fixture f = new Fixture(new ReadyFixture())) {
            RadioEventSession session=new RadioEventSession(77,f.endpoint);
            // Synthetic GSM SMS-DELIVER, no SMSC, sender +10086, UCS-2 body "A".
            byte[] pdu={0,4,5,(byte)0x91,1,(byte)0x80,(byte)0xf6,0,8,0x62,0x11,0x70,0x21,0,0,0,2,0,0x41};
            byte[] sms=frame(77,1,1,pdu);
            byte[] ack=session.receive(sms);
            check(ack!=null && ack[23]==1 && f.smsEvents.get()==1,"ready SMS not submitted");
            check(Arrays.equals(pdu, f.deliveredPdu.get()), "SMS PDU changed");
            check(session.receive(sms)==null && f.smsEvents.get()==1,"duplicate SMS executed");
            byte[] call=frame(77,2,2,"+10086".getBytes(StandardCharsets.US_ASCII));
            ack=session.receive(call);
            check(ack!=null && ack[23]==1 && f.rings.get()==1,"ready call not submitted");
            check(session.receive(call)==null && f.rings.get()==1,"duplicate call executed");
        }
        try (Fixture f = new Fixture(new ReadyFixture())) {
            f.disconnectDuringVoice = true;
            check(f.endpoint.dispatch(2, "+10086".getBytes(StandardCharsets.US_ASCII)) == RadioEventEndpoint.Outcome.UNCERTAIN,
                    "disconnect during call must be uncertain");
            check(f.rings.get()==0,"call rang after known disconnect");
        }
        try (Fixture f = new Fixture(new ReadyFixture())) {
            f.powerOffDuringSms = true;
            check(f.endpoint.dispatch(1, new byte[]{0,4,11}) == RadioEventEndpoint.Outcome.UNCERTAIN,
                    "power loss during SMS must be uncertain");
            check(f.smsEvents.get()==1 && f.endpoint.dispatch(1, new byte[]{0,4,11})==RadioEventEndpoint.Outcome.REJECTED,
                    "SMS was retried after power loss");
        }
        try (Fixture f = new Fixture(new ReadyFixture())) {
            f.powerCycleDuringSms = true;
            check(f.endpoint.dispatch(1, new byte[]{0,4,11}) == RadioEventEndpoint.Outcome.UNCERTAIN,
                    "power cycle during SMS must be uncertain even if ready again");
            check(f.smsEvents.get()==1,"power cycle repeated SMS");
        }
        System.out.println("RADIO_EVENT_SHARED_STATE_OK");
    }
}
