package app.redmagicemu.wifiprobe;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.os.Binder;
import android.os.Bundle;
import android.os.IBinder;
import android.os.Parcel;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

/** Temporary guest app: all reports contain booleans and error class names only. */
public final class ProbeReceiver extends BroadcastReceiver {
    private static final String IFACE = "android.net.wifi.IWifiManager";
    @Override public void onReceive(Context context, Intent intent) {
        Bundle args = intent.getExtras();
        if (args == null || args.getBinder("service") == null || args.getBinder("response") == null) {
            setResultCode(42);
            return;
        }
        IBinder service = args.getBinder("service");
        IBinder response = args.getBinder("response");
        String report;
        try {
            Parcel data = Parcel.obtain(), reply = Parcel.obtain();
            data.writeInterfaceToken(IFACE);
            if (!service.transact(args.getInt("state"), data, reply, 0)) throw new IllegalStateException("state transaction");
            reply.readException();
            boolean state = reply.readInt() == 3;
            data.recycle(); reply.recycle();

            data = Parcel.obtain(); reply = Parcel.obtain();
            data.writeInterfaceToken(IFACE); data.writeString(context.getPackageName()); data.writeString(null);
            if (!service.transact(args.getInt("results"), data, reply, 0)) throw new IllegalStateException("results transaction");
            reply.readException();
            boolean results = reply.readInt() != 0;
            data.recycle(); reply.recycle();

            final CountDownLatch callbackSeen = new CountDownLatch(1);
            Binder callback = new Binder() {
                @Override protected boolean onTransact(int code, Parcel incoming, Parcel outgoing, int flags) {
                    if (code != IBinder.FIRST_CALL_TRANSACTION) return false;
                    incoming.enforceInterface("android.net.wifi.IScanResultsCallback");
                    callbackSeen.countDown();
                    return true;
                }
            };
            data = Parcel.obtain(); reply = Parcel.obtain();
            data.writeInterfaceToken(IFACE); data.writeStrongBinder(callback);
            if (!service.transact(args.getInt("register"), data, reply, 0)) throw new IllegalStateException("register transaction");
            reply.readException(); data.recycle(); reply.recycle();
            data = Parcel.obtain(); reply = Parcel.obtain();
            data.writeInterfaceToken(IFACE); data.writeString(context.getPackageName()); data.writeString(null);
            if (!service.transact(args.getInt("start"), data, reply, 0)) throw new IllegalStateException("start transaction");
            reply.readException();
            boolean started = reply.readInt() != 0;
            data.recycle(); reply.recycle();
            boolean callbackReceived = started && callbackSeen.await(5, TimeUnit.SECONDS);
            data = Parcel.obtain(); reply = Parcel.obtain();
            data.writeInterfaceToken(IFACE); data.writeStrongBinder(callback);
            if (!service.transact(args.getInt("unregister"), data, reply, 0)) throw new IllegalStateException("unregister transaction");
            reply.readException(); data.recycle(); reply.recycle();
            report = "state=" + state + " results=" + results + " started=" + started + " callback=" + callbackReceived;
        } catch (Throwable failure) { report = "error=" + failure.getClass().getSimpleName(); }
        try {
            Parcel data = Parcel.obtain(), reply = Parcel.obtain();
            data.writeString(report);
            response.transact(IBinder.FIRST_CALL_TRANSACTION, data, reply, 0);
            data.recycle(); reply.recycle();
        } catch (Exception ignored) { }
    }
}
