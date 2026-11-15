package app.aemu.stub;

import android.content.Context;
import android.content.ContextWrapper;
import android.content.Intent;
import android.os.Binder;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.Parcel;
import android.os.RemoteException;

import java.io.File;
import java.io.FileOutputStream;
import java.lang.reflect.Method;
import java.util.Properties;

/** Isolated test endpoint; never registers the wifi service or changes a network. */
public final class WifiPrivilegedConfigServer {
    public static void main(String[] args) throws Exception {
        if (args.length != 1 || !args[0].matches("redmagicemu\\.test\\.wifi\\.[a-zA-Z0-9_]+"))
            throw new IllegalArgumentException("unique redmagicemu.test.wifi.* name required");
        Looper.prepare();
        Looper loop = Looper.myLooper();
        Context context = new ContextWrapper(WifiSystemContext.obtain()) {
            @Override public void sendBroadcast(Intent intent) {
                // The disposable service's state must never reach the running guest.
                if (intent.getAction() != null && intent.getAction().startsWith("android.net.wifi.")) return;
                super.sendBroadcast(intent);
            }
        };
        File config = File.createTempFile("rme-cross-config-", ".properties", new File("/data/local/tmp"));
        try {
            try (FileOutputStream out = new FileOutputStream(config)) { new Properties().store(out, null); }
            WifiService service = new WifiService(WifiConfig.load(config), loop, context);
            service.setListener(new WifiService.Listener() {
                @Override public boolean onConnectionChanged(boolean connected, Object info) { return true; }
            });
            Binder endpoint = new Binder() {
                @Override protected boolean onTransact(int code, Parcel data, Parcel reply, int flags)
                        throws RemoteException {
                    System.out.println("WIFI_PRIVILEGED_CROSS_CALL uid=" + Binder.getCallingUid()
                            + " pid=" + Binder.getCallingPid());
                    return service.onTransact(code, data, reply, flags);
                }
            };
            Class<?> manager = Class.forName("android.os.ServiceManager");
            if (manager.getMethod("checkService", String.class).invoke(null, args[0]) != null)
                throw new IllegalStateException("diagnostic name already in use");
            Method add = manager.getMethod("addService", String.class, IBinder.class);
            add.invoke(null, args[0], endpoint);
            System.out.println("WIFI_PRIVILEGED_CROSS_READY name=" + args[0]
                    + " uid=" + android.os.Process.myUid() + " pid=" + android.os.Process.myPid());
            System.out.flush();
            new Handler(loop).postDelayed(new Runnable() {
                @Override public void run() { System.out.println("WIFI_PRIVILEGED_CROSS_TIMEOUT"); loop.quitSafely(); }
            }, 60000);
            Looper.loop();
        } finally { if (!config.delete()) System.err.println("could not remove diagnostic properties"); }
    }
}
