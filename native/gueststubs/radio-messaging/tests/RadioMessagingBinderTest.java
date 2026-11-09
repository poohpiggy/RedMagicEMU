package app.aemu.stub;

import android.hardware.radio.RadioError;
import android.hardware.radio.RadioIndicationType;
import android.hardware.radio.RadioResponseInfo;
import android.hardware.radio.messaging.IRadioMessaging;
import android.hardware.radio.messaging.IRadioMessagingIndication;
import android.hardware.radio.messaging.IRadioMessagingResponse;
import android.os.Binder;
import android.os.IBinder;
import android.os.Parcel;
import android.os.RemoteException;
import java.lang.reflect.Field;
import java.lang.reflect.Proxy;
import java.util.Arrays;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

/** Isolated cross-process test using only a temporary synthetic bootstrap service. */
public final class RadioMessagingBinderTest {
    private static final String BOOTSTRAP = "app.redmagicemu.test.messaging.contract0610";
    private static final byte[] PDU = new byte[]{0, 4, 11, 42}; // synthetic bytes, not a valid SMS PDU
    private static final CountDownLatch serverDone = new CountDownLatch(1);
    private static final CountDownLatch indicationDone = new CountDownLatch(1);
    private static final CountDownLatch responseDone = new CountDownLatch(1);
    private static volatile boolean indicationValid, responseValid;

    private static Object manager(String method, Class<?>[] types, Object... args) throws Exception {
        return Class.forName("android.os.ServiceManager").getMethod(method, types).invoke(null, args);
    }
    private static int transaction(String iface, String method) throws Exception {
        Field f = Class.forName(iface + "$Stub").getDeclaredField("TRANSACTION_" + method);
        f.setAccessible(true);
        return f.getInt(null);
    }
    private static final class IndicationBinder extends Binder {
        final int newSms;
        IndicationBinder() throws Exception { newSms = transaction(IRadioMessagingIndication.class.getName(), "newSms"); }
        @Override protected boolean onTransact(int code, Parcel data, Parcel reply, int flags) throws RemoteException {
            if (code == IBinder.INTERFACE_TRANSACTION) { if (reply != null) reply.writeString(IRadioMessagingIndication.DESCRIPTOR); return true; }
            if (code != newSms) return false;
            data.enforceInterface(IRadioMessagingIndication.DESCRIPTOR);
            int type = data.readInt(); byte[] bytes = data.createByteArray();
            indicationValid = type == RadioIndicationType.UNSOLICITED && Arrays.equals(bytes, PDU);
            indicationDone.countDown();
            return true;
        }
    }
    private static final class ResponseBinder extends Binder {
        final int ack;
        ResponseBinder() throws Exception { ack = transaction(IRadioMessagingResponse.class.getName(), "acknowledgeLastIncomingGsmSmsResponse"); }
        @Override protected boolean onTransact(int code, Parcel data, Parcel reply, int flags) throws RemoteException {
            if (code == IBinder.INTERFACE_TRANSACTION) { if (reply != null) reply.writeString(IRadioMessagingResponse.DESCRIPTOR); return true; }
            if (code != ack) return false;
            data.enforceInterface(IRadioMessagingResponse.DESCRIPTOR);
            RadioResponseInfo info = data.readTypedObject(RadioResponseInfo.CREATOR);
            responseValid = info != null && info.serial == 6101 && info.error == RadioError.NONE;
            responseDone.countDown();
            return true;
        }
    }
    private static Object callbackProxy(Class<?> type, IBinder binder) {
        return Proxy.newProxyInstance(type.getClassLoader(), new Class<?>[]{type}, (p,m,a) -> {
            if (m.getName().equals("asBinder")) return binder;
            if (m.getName().equals("toString")) return "synthetic-callback";
            throw new UnsupportedOperationException(m.getName());
        });
    }
    private static void server() throws Exception {
        if (manager("checkService", new Class<?>[]{String.class}, BOOTSTRAP) != null)
            throw new IllegalStateException("test bootstrap already exists");
        final RadioMessagingService sms = RadioMessagingService.withoutStateForTests("slot1");
        Binder bootstrap = new Binder() {
            @Override protected boolean onTransact(int code, Parcel data, Parcel reply, int flags) {
                if (code == 1) { reply.writeNoException(); reply.writeStrongBinder(sms.asBinder()); return true; }
                if (code == 2) { reply.writeNoException(); serverDone.countDown(); return true; }
                if (code == 3) {
                    try { sms.injectIncomingGsmSms(PDU); reply.writeNoException(); reply.writeInt(1); }
                    catch (RemoteException unavailable) { reply.writeNoException(); reply.writeInt(0); }
                    return true;
                }
                return false;
            }
        };
        manager("addService", new Class<?>[]{String.class, IBinder.class}, BOOTSTRAP, bootstrap);
        System.out.println("SMS_BOOTSTRAP_REGISTERED"); System.out.flush();
        if (!serverDone.await(40, TimeUnit.SECONDS)) throw new IllegalStateException("client timed out");
        Thread.sleep(4000);
        try { sms.getSmscAddress(6102); throw new AssertionError("dead callbacks retained"); }
        catch (RemoteException expected) {
            if (!"radio messaging is not connected".equals(expected.getMessage())) throw expected;
        }
        System.out.println("MESSAGING_BINDER_SERVER_OK"); System.out.flush();
    }
    private static void client() throws Exception {
        IBinder bootstrap = (IBinder) manager("checkService", new Class<?>[]{String.class}, BOOTSTRAP);
        if (bootstrap == null) throw new IllegalStateException("test bootstrap missing");
        Parcel data = Parcel.obtain(), reply = Parcel.obtain(); IBinder binder;
        try {
            if (!bootstrap.transact(1, data, reply, 0)) throw new IllegalStateException("bootstrap rejected");
            reply.readException(); binder = reply.readStrongBinder();
        } finally { data.recycle(); reply.recycle(); }
        if (binder == null || binder.queryLocalInterface(IRadioMessaging.DESCRIPTOR) != null)
            throw new AssertionError("messaging binder was not remote");
        IRadioMessaging sms = IRadioMessaging.Stub.asInterface(binder);
        IRadioMessagingResponse response = (IRadioMessagingResponse) callbackProxy(IRadioMessagingResponse.class, new ResponseBinder());
        IRadioMessagingIndication indication = (IRadioMessagingIndication) callbackProxy(IRadioMessagingIndication.class, new IndicationBinder());
        sms.setResponseFunctions(response, indication);
        boolean triggered = false;
        for (int i = 0; i < 40 && !triggered; i++) {
            data = Parcel.obtain(); reply = Parcel.obtain();
            try {
                if (!bootstrap.transact(3, data, reply, 0)) throw new IllegalStateException("trigger rejected");
                reply.readException(); triggered = reply.readInt() == 1;
            } finally { data.recycle(); reply.recycle(); }
            if (!triggered) Thread.sleep(50);
        }
        if (!triggered || !indicationDone.await(20, TimeUnit.SECONDS) || !indicationValid)
            throw new AssertionError("bad or missing PDU indication");
        sms.acknowledgeLastIncomingGsmSms(6101, true, 0);
        if (!responseDone.await(20, TimeUnit.SECONDS) || !responseValid)
            throw new AssertionError("bad or missing ACK response");
        data = Parcel.obtain(); reply = Parcel.obtain();
        try {
            if (!bootstrap.transact(2, data, reply, 0)) throw new IllegalStateException("finish rejected");
            reply.readException();
        } finally { data.recycle(); reply.recycle(); }
        System.out.println("MESSAGING_BINDER_CLIENT_OK"); System.out.flush();
        System.exit(0);
    }
    public static void main(String[] args) throws Exception {
        if (args.length != 1) throw new IllegalArgumentException("mode");
        if (args[0].equals("server")) server();
        else if (args[0].equals("client")) client();
        else throw new IllegalArgumentException("mode");
    }
}
