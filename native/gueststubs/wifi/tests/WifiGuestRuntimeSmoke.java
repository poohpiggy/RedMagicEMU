package app.aemu.stub;

import android.content.Context;
import android.net.wifi.ScanResult;
import android.net.wifi.WifiInfo;
import android.os.Looper;
import android.os.Parcelable;

import java.io.File;
import java.io.FileOutputStream;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.List;
import java.util.Properties;

/** Standalone guest diagnostic: does not register a service or touch network state. */
public final class WifiGuestRuntimeSmoke {
    private static void require(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }
    public static void main(String[] args) throws Exception {
        Looper.prepare();
        Context context = WifiSystemContext.obtain();
        require(context != null, "system Context unavailable");
        require(context.getSystemService(Context.APP_OPS_SERVICE) != null, "AppOps unavailable");
        File temp = File.createTempFile("rme-wifi-smoke-", ".properties", new File("/data/local/tmp"));
        try {
            Properties properties = new Properties();
            try (FileOutputStream output = new FileOutputStream(temp)) { properties.store(output, null); }
            WifiConfig config = WifiConfig.load(temp);
            require(config != null && "RedMagicEMU".equals(config.ssid), "configuration unavailable");
            WifiService service = new WifiService(config, Looper.myLooper(), context);
            // A queued network callback must not clear an earlier looper failure.
            System.out.println("WIFI_DIAG_BEFORE_FAILURE");
            System.out.flush();
            service.onNetworkFailure(new IllegalStateException("diagnostic looper stopped"));
            System.out.println("WIFI_DIAG_BEFORE_UNWANTED");
            System.out.flush();
            service.onNetworkUnwanted();
            System.out.println("WIFI_DIAG_AFTER_UNWANTED");
            System.out.flush();
            boolean unknown = false;
            try { service.isConnected(); } catch (IllegalStateException expected) { unknown = true; }
            require(unknown, "queued network callback cleared a prior failure");
            Object info = service.wifiInfo(true);
            require(info instanceof WifiInfo, "WifiInfo unavailable");
            WifiInfo wifi = (WifiInfo) info;
            require("\"RedMagicEMU\"".equals(wifi.getSSID()), "WifiInfo SSID mismatch");
            require(config.bssid.equals(wifi.getBSSID()), "WifiInfo BSSID mismatch");
            Method isPrimary = WifiInfo.class.getMethod("isPrimary");
            require(Boolean.TRUE.equals(isPrimary.invoke(wifi)), "associated virtual Wi-Fi is not primary");
            require(Boolean.FALSE.equals(isPrimary.invoke(service.wifiInfo(false))),
                    "disconnected Wi-Fi claims primary");
            Object redacted = WifiPrivacy.fullyRedacted(info);
            require(redacted instanceof WifiInfo, "redacted WifiInfo unavailable");
            require(!"\"RedMagicEMU\"".equals(((WifiInfo) redacted).getSSID()), "SSID not redacted");
            try {
                isPrimary.invoke(redacted);
                throw new AssertionError("primary identity leaked without NETWORK_SETTINGS");
            } catch (InvocationTargetException expected) {
                require(expected.getCause() instanceof SecurityException, "unexpected redaction error");
            }
            Object settingsInfo = WifiPrivacy.redactedFor(info, true, true, true);
            require(Boolean.TRUE.equals(isPrimary.invoke(settingsInfo)), "settings caller lost primary identity");
            Method scan = WifiService.class.getDeclaredMethod("virtualScanResult");
            scan.setAccessible(true);
            ScanResult result = (ScanResult) scan.invoke(service);
            require(result.getWifiSsid() != null, "scan WifiSsid unavailable");
            require("RedMagicEMU".equals(new String(result.getWifiSsid().getBytes(), "UTF-8")), "scan SSID mismatch");
            List<Parcelable> results = new ArrayList<Parcelable>();
            results.add(result);
            Method slice = WifiService.class.getDeclaredMethod("parcelableSlice", String.class, List.class);
            slice.setAccessible(true);
            require(slice.invoke(service, "getScanResults", results) instanceof Parcelable, "scan list unavailable");
            System.out.println("WIFI_GUEST_RUNTIME_SMOKE_OK");
        } finally {
            if (!temp.delete()) System.err.println("could not remove diagnostic properties");
        }
    }
}
