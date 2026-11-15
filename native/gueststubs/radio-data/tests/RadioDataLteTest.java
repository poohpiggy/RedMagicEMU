package app.aemu.stub;

import android.hardware.radio.RadioError;
import android.hardware.radio.RadioIndicationType;
import android.hardware.radio.RadioResponseInfo;
import android.hardware.radio.data.IRadioDataIndication;
import android.hardware.radio.data.IRadioDataResponse;
import android.hardware.radio.data.SetupDataCallResult;
import android.os.IBinder;
import java.lang.reflect.Proxy;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.concurrent.atomic.AtomicInteger;

/** Verifies the opt-in virtual LTE data call without creating a real cellular bearer. */
public final class RadioDataLteTest {
    private static void check(boolean value, String message) {
        if (!value) throw new AssertionError(message);
    }

    private static IBinder peer() {
        return (IBinder) Proxy.newProxyInstance(IBinder.class.getClassLoader(), new Class<?>[]{IBinder.class},
                (proxy, method, args) -> {
                    switch (method.getName()) {
                        case "linkToDeath": return null;
                        case "unlinkToDeath": return true;
                        case "isBinderAlive": return true;
                        case "hashCode": return System.identityHashCode(proxy);
                        case "equals": return proxy == args[0];
                        default: throw new UnsupportedOperationException(method.getName());
                    }
                });
    }

    public static void main(String[] args) throws Exception {
        Path gate = Files.createTempFile("rme-radio-lte-gate-", ".state");
        System.setProperty(RadioNetworkGate.PATH_PROPERTY, gate.toString());
        try {
            SyntheticSingleSlot slot = SyntheticSingleSlot.forTests(true);
            RadioDataService data = new RadioDataService("slot1", slot, true);
            IBinder responseBinder = peer(), indicationBinder = peer();
            final RadioResponseInfo[] response = {null};
            final Object[] value = {null};
            final AtomicInteger listChanges = new AtomicInteger();
            final AtomicInteger emptyIndications = new AtomicInteger();
            IRadioDataResponse callbacks = (IRadioDataResponse) Proxy.newProxyInstance(
                    IRadioDataResponse.class.getClassLoader(), new Class<?>[]{IRadioDataResponse.class},
                    (proxy, method, methodArgs) -> {
                        if (method.getName().equals("asBinder")) return responseBinder;
                        response[0] = (RadioResponseInfo) methodArgs[0];
                        value[0] = methodArgs.length > 1 ? methodArgs[1] : null;
                        return null;
                    });
            IRadioDataIndication indication = (IRadioDataIndication) Proxy.newProxyInstance(
                    IRadioDataIndication.class.getClassLoader(), new Class<?>[]{IRadioDataIndication.class},
                    (proxy, method, methodArgs) -> {
                        if (method.getName().equals("asBinder")) return indicationBinder;
                        if (!method.getName().equals("dataCallListChanged")) {
                            throw new AssertionError("unexpected indication " + method.getName());
                        }
                        check(((Number) methodArgs[0]).intValue() == RadioIndicationType.UNSOLICITED,
                                "solicited data call indication");
                        listChanges.incrementAndGet();
                        if (((SetupDataCallResult[]) methodArgs[1]).length == 0) emptyIndications.incrementAndGet();
                        return null;
                    });
            data.setResponseFunctions(callbacks, indication);
            slot.setRadioPower(true);

            data.setupDataCall(10, 0, null, false, 0, null, null, 17, null, true);
            check(response[0].serial == 10 && response[0].error == RadioError.NONE, "LTE data setup failed");
            SetupDataCallResult active = (SetupDataCallResult) value[0];
            check(active != null && active.active == SetupDataCallResult.DATA_CONNECTION_STATUS_ACTIVE,
                    "data call is not active");
            check(active.cid > 0 && active.ifname.equals("eth0"), "data call has no usable interface");
            check(active.addresses.length == 1 && active.addresses[0].address.equals("10.0.2.15/24"),
                    "unexpected address");
            check(active.gateways.length == 1 && active.gateways[0].equals("10.0.2.2"), "unexpected gateway");
            check(active.dnses.length == 2 && active.dnses[0].equals("8.8.8.8"), "unexpected DNS");
            check(active.pduSessionId == 17 && active.mtuV4 == 1500, "unexpected session or MTU");
            check("1\n".equals(new String(Files.readAllBytes(gate), StandardCharsets.US_ASCII)),
                    "connected LTE did not open the Internet gate");

            data.getDataCallList(11);
            SetupDataCallResult[] listed = (SetupDataCallResult[]) value[0];
            check(response[0].error == RadioError.NONE && listed.length == 1 && listed[0].cid == active.cid,
                    "active data call missing from list");

            data.deactivateDataCall(12, active.cid, 0);
            check(response[0].error == RadioError.NONE, "data call deactivation failed");
            check("0\n".equals(new String(Files.readAllBytes(gate), StandardCharsets.US_ASCII)),
                    "deactivation left the Internet gate open");
            check(listChanges.get() == 1 && emptyIndications.get() == 1, "deactivation indication missing");

            data.setupDataCall(13, 0, null, false, 0, null, null, 18, null, true);
            check(response[0].error == RadioError.NONE, "second data setup failed");
            data.setDataAllowed(14, false);
            check(response[0].error == RadioError.NONE, "data disallow failed");
            check("0\n".equals(new String(Files.readAllBytes(gate), StandardCharsets.US_ASCII)),
                    "mobile-data-off left the Internet gate open");

            data.setDataAllowed(15, true);
            data.setupDataCall(16, 0, null, false, 0, null, null, 19, null, true);
            check(response[0].error == RadioError.NONE, "data setup after re-enable failed");
            slot.setRadioPower(false);
            check("0\n".equals(new String(Files.readAllBytes(gate), StandardCharsets.US_ASCII)),
                    "radio off left the Internet gate open");
            data.getDataCallList(17);
            check(((SetupDataCallResult[]) value[0]).length == 0, "radio off retained an active data call");
            data.close();
            System.out.println("RADIO_DATA_LTE_OK");
        } finally {
            Files.deleteIfExists(gate);
        }
    }
}
