package app.aemu.stub;

import android.content.Context;
import android.content.ContextWrapper;
import android.content.Intent;
import android.os.Binder;
import android.os.Bundle;
import android.os.IBinder;
import android.os.Handler;
import android.os.Looper;
import android.os.Parcel;

import java.io.File;
import java.io.FileOutputStream;
import java.lang.reflect.Field;
import java.util.Properties;

/** Sends a private test Binder directly to an ordinary app; never publishes the wifi name. */
public final class WifiGuestCrossProcessServer {
    private static int code(String name) throws Exception {
        Field field = Class.forName("android.net.wifi.IWifiManager$Stub").getDeclaredField("TRANSACTION_" + name);
        field.setAccessible(true);
        return field.getInt(null);
    }
    public static void main(String[] args) throws Exception {
        Looper.prepare();
        final Looper loop = Looper.myLooper();
        final Handler handler = new Handler(loop);
        Context base = WifiSystemContext.obtain();
        Context context = new ContextWrapper(base) {
            @Override public void sendBroadcast(Intent intent) {
                // A test scan must not broadcast synthetic state into the running guest.
                if (!"android.net.wifi.SCAN_RESULTS".equals(intent.getAction())) super.sendBroadcast(intent);
            }
        };
        File temp = File.createTempFile("rme-cross-wifi-", ".properties", new File("/data/local/tmp"));
        try {
            try (FileOutputStream out = new FileOutputStream(temp)) { new Properties().store(out, null); }
            WifiService service = new WifiService(WifiConfig.load(temp), loop, context);
            Binder response = new Binder() {
                @Override protected boolean onTransact(int transaction, Parcel data, Parcel reply, int flags) {
                    if (transaction != IBinder.FIRST_CALL_TRANSACTION) return false;
                    System.out.println("WIFI_CROSS_PROCESS " + data.readString());
                    System.out.flush();
                    if (reply != null) reply.writeNoException();
                    handler.post(new Runnable() { @Override public void run() { loop.quitSafely(); } });
                    return true;
                }
            };
            Bundle values = new Bundle();
            values.putBinder("service", service);
            values.putBinder("response", response);
            values.putInt("state", code("getWifiEnabledState"));
            values.putInt("results", code("getScanResults"));
            values.putInt("register", code("registerScanResultsCallback"));
            values.putInt("start", code("startScan"));
            values.putInt("unregister", code("unregisterScanResultsCallback"));
            Intent intent = new Intent();
            intent.setClassName("app.redmagicemu.wifiprobe", "app.redmagicemu.wifiprobe.ProbeReceiver");
            intent.addFlags(Intent.FLAG_INCLUDE_STOPPED_PACKAGES);
            intent.putExtras(values);
            handler.postDelayed(new Runnable() { @Override public void run() {
                System.out.println("WIFI_CROSS_PROCESS timeout"); loop.quitSafely();
            } }, 20000);
            context.sendBroadcast(intent);
            Looper.loop();
        } finally {
            if (!temp.delete()) System.err.println("could not remove diagnostic properties");
        }
    }
}
