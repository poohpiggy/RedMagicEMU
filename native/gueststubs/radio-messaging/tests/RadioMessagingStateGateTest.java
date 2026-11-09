package app.aemu.stub;

import android.hardware.radio.RadioError;
import android.hardware.radio.RadioResponseInfo;
import android.hardware.radio.messaging.IRadioMessagingIndication;
import android.hardware.radio.messaging.IRadioMessagingResponse;
import android.os.IBinder;
import android.os.RemoteException;
import java.lang.reflect.Proxy;

/** Only isolated synthetic callbacks; never registers a service or uses a host SIM. */
public final class RadioMessagingStateGateTest {
    private static void check(boolean value, String reason) { if (!value) throw new AssertionError(reason); }
    private static IBinder peer() {
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
    private static void rejected(RadioMessagingService service) throws Exception {
        try { service.injectIncomingGsmSms(new byte[]{0, 4, 11}); throw new AssertionError("unavailable model accepted SMS"); }
        catch (RemoteException expected) { /* no indication was accepted */ }
    }
    public static void main(String[] args) throws Exception {
        try { new RadioMessagingService("slot1", null); throw new AssertionError("missing model accepted"); }
        catch (IllegalArgumentException expected) { }
        SyntheticSingleSlot model = SyntheticSingleSlot.forTests();
        RadioMessagingService service = new RadioMessagingService("slot1", model);
        final int[] indications = {0}; final RadioResponseInfo[] response = {null};
        IBinder rb=peer(), ib=peer();
        IRadioMessagingResponse r=(IRadioMessagingResponse)Proxy.newProxyInstance(IRadioMessagingResponse.class.getClassLoader(),new Class<?>[]{IRadioMessagingResponse.class},(p,m,a) -> {
            if (m.getName().equals("asBinder")) return rb;
            if (a!=null && a.length>0 && a[0] instanceof RadioResponseInfo) response[0]=(RadioResponseInfo)a[0];
            return null;
        });
        IRadioMessagingIndication i=(IRadioMessagingIndication)Proxy.newProxyInstance(IRadioMessagingIndication.class.getClassLoader(),new Class<?>[]{IRadioMessagingIndication.class},(p,m,a) -> {
            if (m.getName().equals("asBinder")) return ib;
            indications[0]++; return null;
        });
        service.setResponseFunctions(r,i);
        rejected(service); // OFF
        check(indications[0]==0, "OFF emitted a message");
        model.setRadioPower(true);
        service.injectIncomingGsmSms(new byte[]{0, 4, 11}); // virtual card and network ready
        check(indications[0]==1, "ready synthetic radio did not invoke messaging callback");
        service.acknowledgeLastIncomingGsmSms(899,true,0);
        check(response[0]!=null && response[0].serial==899 && response[0].error==RadioError.NONE,
                "incoming callback was not acknowledged");
        model.disconnect();
        rejected(service);
        check(indications[0]==1, "disconnected radio emitted a message");
        service.acknowledgeLastIncomingGsmSms(900,true,0);
        check(response[0]!=null && response[0].serial==900 && response[0].error==RadioError.INVALID_STATE,
                "a rejected event left a pending acknowledgement");
        System.out.println("RADIO_MESSAGING_STATE_GATE_OK");
    }
}
