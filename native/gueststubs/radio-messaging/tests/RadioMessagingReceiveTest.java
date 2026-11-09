package app.aemu.stub;

import android.hardware.radio.RadioError;
import android.hardware.radio.RadioIndicationType;
import android.hardware.radio.RadioResponseInfo;
import android.hardware.radio.messaging.IRadioMessagingIndication;
import android.hardware.radio.messaging.IRadioMessagingResponse;
import android.os.IBinder;
import android.os.RemoteException;

import java.lang.reflect.Proxy;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/** Test-only incoming PDU and acknowledgement; never registers a radio service. */
public final class RadioMessagingReceiveTest {
    private static void check(boolean ok, String reason) {
        if (!ok) throw new AssertionError(reason);
    }
    private static final class Peer {
        final List<IBinder.DeathRecipient> deaths = new ArrayList<>();
        final IBinder binder = (IBinder) Proxy.newProxyInstance(IBinder.class.getClassLoader(),
                new Class<?>[]{IBinder.class}, (p,m,a) -> {
                    switch (m.getName()) {
                        case "linkToDeath": deaths.add((IBinder.DeathRecipient)a[0]); return null;
                        case "unlinkToDeath": return deaths.remove(a[0]);
                        case "isBinderAlive": return true;
                        case "hashCode": return System.identityHashCode(p);
                        case "equals": return p == a[0];
                        case "toString": return "synthetic-peer";
                        default: throw new UnsupportedOperationException(m.getName());
                    }
                });
        void die() { for (IBinder.DeathRecipient d : new ArrayList<>(deaths)) d.binderDied(); }
    }
    private static final class Callbacks {
        final Peer responsePeer = new Peer(), indicationPeer = new Peer();
        byte[] pdu;
        int indicationType = -1, count;
        boolean failIndication;
        RadioResponseInfo info;
        String responseName;
        final IRadioMessagingResponse response = (IRadioMessagingResponse) Proxy.newProxyInstance(
                IRadioMessagingResponse.class.getClassLoader(), new Class<?>[]{IRadioMessagingResponse.class},
                (p,m,a) -> {
                    if (m.getName().equals("asBinder")) return responsePeer.binder;
                    if (m.getName().equals("toString")) return "synthetic-response";
                    responseName = m.getName();
                    info = a != null && a.length > 0 && a[0] instanceof RadioResponseInfo ? (RadioResponseInfo)a[0] : null;
                    return null;
                });
        final IRadioMessagingIndication indication = (IRadioMessagingIndication) Proxy.newProxyInstance(
                IRadioMessagingIndication.class.getClassLoader(), new Class<?>[]{IRadioMessagingIndication.class},
                (p,m,a) -> {
                    if (m.getName().equals("asBinder")) return indicationPeer.binder;
                    if (m.getName().equals("toString")) return "synthetic-indication";
                    if (!m.getName().equals("newSms")) throw new AssertionError("unexpected indication");
                    if (failIndication) throw new RemoteException("synthetic indication failure");
                    indicationType = (Integer)a[0]; pdu = (byte[])a[1]; count++;
                    return null;
                });
    }
    private static void expectRejected(RadioMessagingService s, byte[] pdu) throws Exception {
        try { s.injectIncomingGsmSms(pdu); throw new AssertionError("invalid PDU accepted"); }
        catch (IllegalArgumentException expected) { /* input rejected before any event */ }
    }
    public static void main(String[] args) throws Exception {
        RadioMessagingService s = RadioMessagingService.withoutStateForTests("slot1");
        try { s.injectIncomingGsmSms(new byte[]{0}); throw new AssertionError("unbound accepted"); }
        catch (RemoteException expected) { /* cannot send without client */ }
        Callbacks first = new Callbacks(); s.setResponseFunctions(first.response, first.indication);
        expectRejected(s, null); expectRejected(s, new byte[0]); expectRejected(s, new byte[257]);
        byte[] pdu = new byte[]{0, 4, 11}; s.injectIncomingGsmSms(pdu); pdu[1] = 99;
        check(first.count == 1 && first.indicationType == RadioIndicationType.UNSOLICITED &&
                Arrays.equals(first.pdu, new byte[]{0,4,11}), "wrong or mutable indication");
        try { s.injectIncomingGsmSms(new byte[]{1}); throw new AssertionError("second pending PDU accepted"); }
        catch (IllegalStateException expected) { /* one in flight */ }
        s.acknowledgeLastIncomingGsmSms(100, true, 0);
        check("acknowledgeLastIncomingGsmSmsResponse".equals(first.responseName) && first.info.serial == 100 &&
                first.info.error == RadioError.NONE, "successful ack not correlated");
        s.acknowledgeLastIncomingGsmSms(101, true, 0);
        check(first.info.serial == 101 && first.info.error == RadioError.INVALID_STATE, "unsolicited ack accepted");
        s.injectIncomingGsmSms(new byte[]{3});
        Callbacks next = new Callbacks(); s.setResponseFunctions(next.response, next.indication);
        next.info = null; s.acknowledgeLastIncomingGsmSms(102, true, 0);
        check(next.info.serial == 102 && next.info.error == RadioError.INVALID_STATE, "stale pending survived rebind");
        next.failIndication = true;
        try { s.injectIncomingGsmSms(new byte[]{7}); throw new AssertionError("failed indication accepted"); }
        catch (RemoteException expected) { /* peer may have received it: do not retry blindly */ }
        try { s.injectIncomingGsmSms(new byte[]{8}); throw new AssertionError("ambiguous failure retried"); }
        catch (IllegalStateException expected) { /* awaiting acknowledgement or new binding */ }
        next.indicationPeer.die();
        try { s.injectIncomingGsmSms(new byte[]{9}); throw new AssertionError("dead callback accepted"); }
        catch (RemoteException expected) { /* death clears callback and pending state */ }
        Callbacks last = new Callbacks(); s.setResponseFunctions(last.response, last.indication);
        s.injectIncomingGsmSms(new byte[]{10}); s.acknowledgeLastIncomingGsmSms(103, false, 0);
        check(last.info.serial == 103 && last.info.error == RadioError.NONE, "negative ack not accepted");
        System.out.println("RADIO_MESSAGING_RECEIVE_OK");
    }
}
