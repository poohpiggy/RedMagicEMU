package app.aemu.stub;

import android.hardware.radio.RadioError;
import android.hardware.radio.RadioResponseInfo;
import android.hardware.radio.voice.Call;
import android.hardware.radio.voice.IRadioVoice;
import android.hardware.radio.voice.IRadioVoiceIndication;
import android.hardware.radio.voice.IRadioVoiceResponse;
import android.os.Binder;
import android.os.IBinder;
import android.os.Parcel;
import android.os.RemoteException;
import java.lang.reflect.Field;
import java.lang.reflect.Proxy;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;

/** Cross-process test using a synthetic bootstrap, never the real slot1 service name. */
public final class RadioVoiceBinderContractTest {
    private static final String BOOTSTRAP = "app.redmagicemu.test.voice.contract0601";
    private static final CountDownLatch done = new CountDownLatch(1);
    private static final LinkedBlockingQueue<Result> responses = new LinkedBlockingQueue<>();
    private static final LinkedBlockingQueue<String> indications = new LinkedBlockingQueue<>();

    private static final class Result {
        final String name; final RadioResponseInfo info; final Call[] calls;
        Result(String n, RadioResponseInfo i, Call[] c) { name = n; info = i; calls = c; }
    }

    private static Result expect(String name, int serial, int error) throws Exception {
        Result r = responses.poll(15, TimeUnit.SECONDS);
        if (r == null || !name.equals(r.name) || r.info == null || r.info.serial != serial || r.info.error != error)
            throw new AssertionError("missing or incorrect response: " + name);
        return r;
    }

    private static void expectIndication(String expected) throws Exception {
        if (!expected.equals(indications.poll(15, TimeUnit.SECONDS)))
            throw new AssertionError("missing or incorrect indication: " + expected);
    }

    private static Object manager(String method, Class<?>[] types, Object... args) throws Exception {
        return Class.forName("android.os.ServiceManager").getMethod(method, types).invoke(null, args);
    }

    private static int transaction(String iface, String method) throws Exception {
        Field f = Class.forName(iface + "$Stub").getDeclaredField("TRANSACTION_" + method);
        f.setAccessible(true);
        return f.getInt(null);
    }

    private static final class ResponseBinder extends Binder {
        final int calls, accept, hangup, mute;
        ResponseBinder() throws Exception {
            calls = transaction(IRadioVoiceResponse.class.getName(), "getCurrentCallsResponse");
            accept = transaction(IRadioVoiceResponse.class.getName(), "acceptCallResponse");
            hangup = transaction(IRadioVoiceResponse.class.getName(), "hangupConnectionResponse");
            mute = transaction(IRadioVoiceResponse.class.getName(), "getMuteResponse");
        }
        @Override protected boolean onTransact(int code, Parcel data, Parcel reply, int flags) throws RemoteException {
            if (code == IBinder.INTERFACE_TRANSACTION) { reply.writeString(IRadioVoiceResponse.DESCRIPTOR); return true; }
            if (code != calls && code != accept && code != hangup && code != mute) return false;
            data.enforceInterface(IRadioVoiceResponse.DESCRIPTOR);
            RadioResponseInfo info = data.readTypedObject(RadioResponseInfo.CREATOR);
            Call[] list = code == calls ? data.createTypedArray(Call.CREATOR) : null;
            responses.add(new Result(code == calls ? "calls" : code == accept ? "accept" : code == hangup ? "hangup" : "mute", info, list));
            return true;
        }
    }

    private static final class IndicationBinder extends Binder {
        final int state, ring;
        IndicationBinder() throws Exception {
            state = transaction(IRadioVoiceIndication.class.getName(), "callStateChanged");
            ring = transaction(IRadioVoiceIndication.class.getName(), "callRing");
        }
        @Override protected boolean onTransact(int code, Parcel data, Parcel reply, int flags) throws RemoteException {
            if (code == IBinder.INTERFACE_TRANSACTION) { reply.writeString(IRadioVoiceIndication.DESCRIPTOR); return true; }
            if (code != state && code != ring) return false;
            data.enforceInterface(IRadioVoiceIndication.DESCRIPTOR);
            int type = data.readInt();
            boolean isGsm = code != ring || data.readBoolean();
            indications.add(type == android.hardware.radio.RadioIndicationType.UNSOLICITED && isGsm
                    ? (code == state ? "state" : "ring") : "invalid");
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
        final RadioVoiceService voice = new RadioVoiceService("slot1");
        Binder bootstrap = new Binder() {
            @Override protected boolean onTransact(int code, Parcel data, Parcel reply, int flags) throws RemoteException {
                if (code == 1) { reply.writeNoException(); reply.writeStrongBinder(voice.asBinder()); return true; }
                if (code == 2) { reply.writeNoException(); done.countDown(); return true; }
                if (code == 3) { boolean accepted = voice.injectIncoming("+10000000000"); reply.writeNoException(); reply.writeBoolean(accepted); return true; }
                return false;
            }
        };
        manager("addService", new Class<?>[]{String.class, IBinder.class}, BOOTSTRAP, bootstrap);
        System.out.println("BOOTSTRAP_REGISTERED"); System.out.flush();
        if (!done.await(40, TimeUnit.SECONDS)) throw new IllegalStateException("client timed out");
        Thread.sleep(4000);
        try { voice.getCurrentCalls(4102); throw new AssertionError("dead callbacks retained"); }
        catch (RemoteException expected) {
            if (!"radio voice is not connected".equals(expected.getMessage())) throw expected;
        }
        System.out.println("VOICE_BINDER_SERVER_OK"); System.out.flush();
    }

    private static void client() throws Exception {
        IBinder bootstrap = (IBinder) manager("checkService", new Class<?>[]{String.class}, BOOTSTRAP);
        if (bootstrap == null) throw new IllegalStateException("test bootstrap missing");
        Parcel data = Parcel.obtain(), reply = Parcel.obtain();
        IBinder binder;
        try {
            if (!bootstrap.transact(1, data, reply, 0)) throw new IllegalStateException("bootstrap rejected");
            reply.readException(); binder = reply.readStrongBinder();
        } finally { data.recycle(); reply.recycle(); }
        if (binder == null || binder.queryLocalInterface(IRadioVoice.DESCRIPTOR) != null)
            throw new AssertionError("voice binder was not remote");
        IRadioVoice voice = IRadioVoice.Stub.asInterface(binder);
        IRadioVoiceResponse response = (IRadioVoiceResponse) callbackProxy(IRadioVoiceResponse.class, new ResponseBinder());
        IRadioVoiceIndication indication = (IRadioVoiceIndication) callbackProxy(IRadioVoiceIndication.class, new IndicationBinder());
        voice.setResponseFunctions(response, indication);
        voice.getCurrentCalls(4101);
        if (expect("calls", 4101, RadioError.NONE).calls.length != 0) throw new AssertionError("initial calls");
        data = Parcel.obtain(); reply = Parcel.obtain();
        try {
            if (!bootstrap.transact(3, data, reply, 0)) throw new IllegalStateException("inject rejected");
            reply.readException(); if (!reply.readBoolean()) throw new AssertionError("incoming rejected");
        } finally { data.recycle(); reply.recycle(); }
        expectIndication("state"); expectIndication("ring");
        voice.getCurrentCalls(4102);
        Call[] incoming = expect("calls", 4102, RadioError.NONE).calls;
        if (incoming.length != 1 || incoming[0].state != Call.STATE_INCOMING || !"+10000000000".equals(incoming[0].number))
            throw new AssertionError("incoming state");
        voice.acceptCall(4103); expect("accept", 4103, RadioError.NONE); expectIndication("state");
        voice.hangup(4104, 1); expect("hangup", 4104, RadioError.NONE); expectIndication("state");
        voice.getCurrentCalls(4105);
        if (expect("calls", 4105, RadioError.NONE).calls.length != 0) throw new AssertionError("call not cleared");
        voice.getMute(4106); expect("mute", 4106, RadioError.REQUEST_NOT_SUPPORTED);
        data = Parcel.obtain(); reply = Parcel.obtain();
        try {
            if (!bootstrap.transact(2, data, reply, 0)) throw new IllegalStateException("finish rejected");
            reply.readException();
        } finally { data.recycle(); reply.recycle(); }
        System.out.println("VOICE_BINDER_CLIENT_OK"); System.out.flush();
        System.exit(0);
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 1) throw new IllegalArgumentException("mode");
        if (args[0].equals("server")) server();
        else if (args[0].equals("client")) client();
        else throw new IllegalArgumentException("mode");
    }
}
