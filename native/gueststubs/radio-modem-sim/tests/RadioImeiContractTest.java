package app.aemu.stub;

import android.hardware.radio.RadioError;
import android.hardware.radio.RadioResponseInfo;
import android.hardware.radio.modem.ImeiInfo;
import android.hardware.radio.modem.IRadioModemIndication;
import android.hardware.radio.modem.IRadioModemResponse;
import android.os.IBinder;
import java.io.File;
import java.lang.reflect.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

/** Synthetic identities only; no device identifiers are read. */
public final class RadioImeiContractTest {
    private static final String IMEI = "490154203237518";
    private static final String SOFTWARE_VERSION = "01";
    private static void check(boolean ok, String message) { if (!ok) throw new AssertionError(message); }
    private static final class Calls implements InvocationHandler {
        final IBinder binder = (IBinder)Proxy.newProxyInstance(IBinder.class.getClassLoader(), new Class<?>[]{IBinder.class},
            (p,m,a) -> { if (m.getName().equals("linkToDeath")) return null; if (m.getName().equals("unlinkToDeath")) return true; throw new UnsupportedOperationException(m.getName()); });
        String name; Object[] args;
        public Object invoke(Object p, Method m, Object[] a) {
            if (m.getName().equals("asBinder")) return binder;
            name=m.getName(); args=a; return null;
        }
        Object proxy(Class<?> type) { return Proxy.newProxyInstance(type.getClassLoader(), new Class<?>[]{type}, this); }
        void expect(String expected, int serial, int error) {
            check(expected.equals(name), "wrong response callback");
            RadioResponseInfo info=(RadioResponseInfo)args[0];
            check(info.serial==serial && info.error==error, "wrong response serial or status");
        }
    }
    public static void main(String[] args) throws Exception {
        Constructor<RadioModemService> constructor;
        try { constructor=RadioModemService.class.getDeclaredConstructor(String.class, SyntheticSingleSlot.class, String.class); }
        catch (NoSuchMethodException missing) { throw new AssertionError("modem cannot accept a private virtual IMEI", missing); }
        SyntheticSingleSlot model=SyntheticSingleSlot.forTests();
        RadioModemService modem=constructor.newInstance("slot1", model, IMEI);
        Calls response=new Calls(), indication=new Calls();
        modem.setResponseFunctions((IRadioModemResponse)response.proxy(IRadioModemResponse.class),
            (IRadioModemIndication)indication.proxy(IRadioModemIndication.class));
        modem.getBasebandVersion(99); response.expect("getBasebandVersionResponse", 99, RadioError.NONE);
        check("RedMagic Virtual Baseband".equals(response.args[1]), "wrong synthetic baseband version");
        modem.getDeviceIdentity(101); response.expect("getDeviceIdentityResponse", 101, RadioError.NONE);
        check(IMEI.equals(response.args[1]), "device identity did not return configured IMEI");
        check(SOFTWARE_VERSION.equals(response.args[2]), "device identity did not return software version");
        modem.getImei(102); response.expect("getImeiResponse", 102, RadioError.NONE);
        ImeiInfo info=(ImeiInfo)response.args[1];
        check(info.type==ImeiInfo.ImeiType.PRIMARY && IMEI.equals(info.imei), "IMEI response did not match configured identity");
        check(SOFTWARE_VERSION.equals(info.svn), "IMEI response software version mismatch");
        model.disconnect();
        modem.getBasebandVersion(100); response.expect("getBasebandVersionResponse", 100, RadioError.RADIO_NOT_AVAILABLE);
        check("".equals(response.args[1]), "disconnected modem exposed a baseband version");
        modem.getDeviceIdentity(103); response.expect("getDeviceIdentityResponse", 103, RadioError.RADIO_NOT_AVAILABLE);
        check("".equals(response.args[1]) && "".equals(response.args[2]), "disconnected device exposed identity");
        modem.getImei(104); response.expect("getImeiResponse", 104, RadioError.RADIO_NOT_AVAILABLE);

        Method read;
        try { read=RadioServiceMain.class.getDeclaredMethod("readImei", File.class); }
        catch (NoSuchMethodException missing) { throw new AssertionError("radio service cannot read its private identity", missing); }
        File file=File.createTempFile("radio-imei-test", ".txt");
        try {
            Files.write(file.toPath(), IMEI.getBytes(StandardCharsets.US_ASCII));
            check(IMEI.equals(read.invoke(null, file)), "valid identity not read");
            for (String invalid : new String[]{"", "490154203237519", "490154203237518\n", "49015420323751x"}) {
                Files.write(file.toPath(), invalid.getBytes(StandardCharsets.US_ASCII));
                try { read.invoke(null, file); throw new AssertionError("invalid identity accepted"); }
                catch (InvocationTargetException expected) { check(expected.getCause() instanceof java.io.IOException, "wrong identity error"); }
            }
        } finally { Files.deleteIfExists(file.toPath()); }
        System.out.println("RADIO_IMEI_CONTRACT_OK");
    }
}
