package app.aemu.stub;

import android.hardware.radio.RadioError;
import android.hardware.radio.RadioResponseInfo;
import android.hardware.radio.voice.IRadioVoice;
import android.hardware.radio.voice.IRadioVoiceIndication;
import android.hardware.radio.voice.IRadioVoiceResponse;
import android.os.Binder;
import android.os.IBinder;
import android.os.Parcel;
import android.os.RemoteException;
import java.lang.reflect.Field;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;

/** Guest-only: actual remote Binder, unique synthetic bootstrap, no slot1 registration. */
public final class RadioTraceBinderTest {
    private static final String NAME = "app.redmagicemu.test.radio.trace0701";
    private static final CountDownLatch DONE = new CountDownLatch(1);
    private static final LinkedBlockingQueue<RadioResponseInfo> RESPONSES = new LinkedBlockingQueue<>();

    private static Object manager(String method, Class<?>[] types, Object... args) throws Exception {
        return Class.forName("android.os.ServiceManager").getMethod(method, types).invoke(null, args);
    }
    private static int tx(Class<?> stub, String name) throws Exception {
        Field f = stub.getDeclaredField("TRANSACTION_" + name); f.setAccessible(true); return f.getInt(null);
    }
    private static void check(boolean ok, String message) { if (!ok) throw new AssertionError(message); }

    private static final class Response extends Binder {
        final int mute;
        Response() throws Exception { mute = tx(IRadioVoiceResponse.Stub.class, "getMuteResponse"); }
        @Override protected boolean onTransact(int code, Parcel data, Parcel reply, int flags) throws RemoteException {
            if (code == INTERFACE_TRANSACTION) { reply.writeString(IRadioVoiceResponse.DESCRIPTOR); return true; }
            if (code != mute) return false;
            data.enforceInterface(IRadioVoiceResponse.DESCRIPTOR);
            RadioResponseInfo info = data.readTypedObject(RadioResponseInfo.CREATOR);
            data.readBoolean();
            RESPONSES.add(info);
            return true;
        }
    }
    private static void expect(int serial) throws Exception {
        RadioResponseInfo info = RESPONSES.poll(15, TimeUnit.SECONDS);
        check(info != null && info.serial == serial && info.error == RadioError.REQUEST_NOT_SUPPORTED,
                "request/reply changed");
    }
    private static void command(IBinder b, int code) throws Exception {
        Parcel data = Parcel.obtain(), reply = Parcel.obtain();
        try { check(b.transact(code, data, reply, 0), "bootstrap failed"); reply.readException(); }
        finally { data.recycle(); reply.recycle(); }
    }
    private static void server() throws Exception {
        check(manager("checkService", new Class<?>[]{String.class}, NAME) == null, "bootstrap collision");
        RadioVoiceService voice = new RadioVoiceService("slot1");
        RadioRequestCounter off = new RadioRequestCounter(false, 128, 64);
        RadioRequestCounter on = new RadioRequestCounter(true, 128, 64);
        voice.setRequestCounterForExperiment(off);
        Binder bootstrap = new Binder() {
            @Override protected boolean onTransact(int code, Parcel data, Parcel reply, int flags) throws RemoteException {
                if (code == 1) { reply.writeNoException(); reply.writeStrongBinder(voice.asBinder()); return true; }
                if (code == 2) { voice.setRequestCounterForExperiment(on); reply.writeNoException(); return true; }
                if (code == 3) { reply.writeNoException(); DONE.countDown(); return true; }
                return false;
            }
        };
        manager("addService", new Class<?>[]{String.class, IBinder.class}, NAME, bootstrap);
        System.out.println("TRACE_BOOTSTRAP_READY"); System.out.flush();
        check(DONE.await(40, TimeUnit.SECONDS), "client timed out");
        RadioRequestCounter.Snapshot before = off.snapshot(), after = on.snapshot();
        check(before.total == 0 && !before.truncated, "disabled trace recorded");
        check(after.counts[tx(IRadioVoice.Stub.class, "setResponseFunctions")] == 1, "binding not counted");
        check(after.counts[tx(IRadioVoice.Stub.class, "getMute")] == 1, "request not counted");
        check(after.other >= 2 && !after.truncated, "version/hash not preserved");
        voice.close();
        System.out.println("RADIO_TRACE_BINDER_SERVER_OK"); System.out.flush();
    }
    private static void client() throws Exception {
        IBinder bootstrap = (IBinder) manager("checkService", new Class<?>[]{String.class}, NAME);
        check(bootstrap != null, "bootstrap absent");
        Parcel data = Parcel.obtain(), reply = Parcel.obtain(); IBinder binder;
        try { check(bootstrap.transact(1, data, reply, 0), "bootstrap failed"); reply.readException(); binder = reply.readStrongBinder(); }
        finally { data.recycle(); reply.recycle(); }
        check(binder != null && binder.queryLocalInterface(IRadioVoice.DESCRIPTOR) == null, "not remote");
        IRadioVoice voice = IRadioVoice.Stub.asInterface(binder);
        IRadioVoiceResponse response = IRadioVoiceResponse.Stub.asInterface(new Response());
        IRadioVoiceIndication indication = IRadioVoiceIndication.Stub.asInterface(new Binder());
        voice.setResponseFunctions(response, indication); voice.getMute(6001); expect(6001);
        command(bootstrap, 2);
        voice.setResponseFunctions(response, indication); voice.getMute(6002); expect(6002);
        check(voice.getInterfaceVersion() == IRadioVoice.VERSION && IRadioVoice.HASH.equals(voice.getInterfaceHash()),
                "version/hash changed");
        command(bootstrap, 3);
        System.out.println("RADIO_TRACE_BINDER_CLIENT_OK"); System.out.flush(); System.exit(0);
    }
    public static void main(String[] args) throws Exception {
        if (args.length != 1) throw new IllegalArgumentException("mode");
        if (args[0].equals("server")) server();
        else if (args[0].equals("client")) client();
        else throw new IllegalArgumentException("mode");
    }
}
