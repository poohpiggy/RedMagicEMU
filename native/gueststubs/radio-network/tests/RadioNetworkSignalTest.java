package app.aemu.stub;

import android.hardware.radio.RadioError;
import android.hardware.radio.RadioConst;
import android.hardware.radio.RadioResponseInfo;
import android.hardware.radio.network.IRadioNetworkIndication;
import android.hardware.radio.network.IRadioNetworkResponse;
import android.hardware.radio.network.SignalStrength;
import android.os.IBinder;
import java.lang.reflect.Proxy;

/** Exercises the synthetic signal contract against the real shared radio state. */
public final class RadioNetworkSignalTest {
    private static void check(boolean ok, String message) { if (!ok) throw new AssertionError(message); }
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
    public static void main(String[] args) throws Exception {
        SyntheticSingleSlot model = SyntheticSingleSlot.forTests();
        RadioNetworkService service = new RadioNetworkService("slot1", model);
        IBinder rb = peer(), ib = peer();
        final RadioResponseInfo[] result = {null};
        final SignalStrength[] strength = {null};
        final int[] allowed = {0};
        IRadioNetworkResponse response = (IRadioNetworkResponse) Proxy.newProxyInstance(IRadioNetworkResponse.class.getClassLoader(), new Class<?>[]{IRadioNetworkResponse.class}, (p,m,a) -> {
            if (m.getName().equals("asBinder")) return rb;
            if (m.getName().equals("getSignalStrengthResponse")) { result[0] = (RadioResponseInfo)a[0]; strength[0] = (SignalStrength)a[1]; return null; }
            if (m.getName().equals("getAllowedNetworkTypesBitmapResponse")) { result[0] = (RadioResponseInfo)a[0]; allowed[0] = (Integer)a[1]; return null; }
            if (m.getName().equals("setAllowedNetworkTypesBitmapResponse")) { result[0] = (RadioResponseInfo)a[0]; return null; }
            throw new AssertionError("unexpected response " + m.getName());
        });
        IRadioNetworkIndication indication = (IRadioNetworkIndication) Proxy.newProxyInstance(IRadioNetworkIndication.class.getClassLoader(), new Class<?>[]{IRadioNetworkIndication.class}, (p,m,a) -> {
            if (m.getName().equals("asBinder")) return ib;
            if (m.getName().equals("currentSignalStrength")) return null;
            throw new AssertionError("unexpected indication " + m.getName());
        });
        service.setResponseFunctions(response, indication);
        model.setRadioPower(true);
        service.getAllowedNetworkTypesBitmap(0);
        check(result[0].error == RadioError.NONE && allowed[0] == (1 << 13), "LTE allowed-network bitmap unavailable");
        service.setAllowedNetworkTypesBitmap(9, (1 << 13) | (1 << 3));
        check(result[0].error == RadioError.NONE, "LTE allowed-network update failed");
        service.getAllowedNetworkTypesBitmap(10);
        check(result[0].error == RadioError.NONE && allowed[0] == (1 << 13), "non-LTE network type was advertised");
        service.getSignalStrength(1);
        check(result[0].serial == 1 && result[0].error == RadioError.NONE, "ON signal query failed");
        check(strength[0] != null && strength[0].lte != null && strength[0].lte.rsrp >= 44 && strength[0].lte.rsrp <= 140, "LTE RSRP unavailable or outside AIDL range");
        check(strength[0].gsm != null && strength[0].gsm.signalStrength == RadioConst.VALUE_UNAVAILABLE
                && strength[0].wcdma != null && strength[0].wcdma.rscp == RadioConst.VALUE_UNAVAILABLE
                && strength[0].nr != null && strength[0].nr.ssRsrp == RadioConst.VALUE_UNAVAILABLE,
                "other radio technologies must be unavailable");
        model.setRadioPower(false);
        service.getSignalStrength(2);
        check(result[0].serial == 2 && result[0].error == RadioError.RADIO_NOT_AVAILABLE, "OFF signal query succeeded");
        check(strength[0].lte != null && strength[0].lte.rsrp == RadioConst.VALUE_UNAVAILABLE, "OFF signal remains valid");
        model.setRadioPower(true);
        service.getSignalStrength(3);
        check(result[0].error == RadioError.NONE && strength[0].lte != null, "signal did not recover");
        model.disconnect();
        service.getSignalStrength(4);
        check(result[0].error == RadioError.RADIO_NOT_AVAILABLE, "disconnected signal query succeeded");
        System.out.println("RADIO_NETWORK_SIGNAL_OK");
    }
}
