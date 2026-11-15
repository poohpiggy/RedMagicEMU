package app.aemu.stub;

import android.app.AppOpsManager;
import android.content.Context;
import android.location.LocationManager;

public final class WifiScanAccessTest {
    private static void check(boolean value, String message) { if (!value) throw new AssertionError(message); }
    public static void main(String[] args) {
        Context context = new Context();
        AppOpsManager ops = new AppOpsManager();
        LocationManager location = new LocationManager();
        context.appOps = ops;
        context.location = location;
        context.changeWifiState = true;
        context.fineLocation = true;
        check(WifiScanAccess.allowed(context, 12345, 222, "valid.package", null, false), "read allowed");
        check(WifiScanAccess.allowed(context, 12345, 222, "valid.package", null, true), "start allowed");
        location.enabled = false;
        check(!WifiScanAccess.allowed(context, 12345, 222, "valid.package", null, false), "location switch");
        location.enabled = true; context.fineLocation = false;
        check(!WifiScanAccess.allowed(context, 12345, 222, "valid.package", null, false), "fine permission");
        context.fineLocation = true; ops.fineMode = 1;
        check(!WifiScanAccess.allowed(context, 12345, 222, "valid.package", null, false), "location app-op");
        ops.fineMode = 0; ops.scanMode = 1;
        check(!WifiScanAccess.allowed(context, 12345, 222, "valid.package", null, false), "scan app-op");
        ops.scanMode = 0; ops.mode = 1;
        check(!WifiScanAccess.allowed(context, 12345, 222, "valid.package", null, true), "change app-op");
        ops.mode = 0; context.changeWifiState = false;
        boolean denied = false;
        try { WifiScanAccess.allowed(context, 12345, 222, "valid.package", null, true); }
        catch (SecurityException expected) { denied = true; }
        check(denied, "change permission");
        context.changeWifiState = true; ops.authorizedUid = 112345;
        check(!WifiScanAccess.allowed(context, 112345, 222, "valid.package", null, false), "cross-user denied");
        ops.authorizedUid = 12345; context.networkSettings = true; context.fineLocation = false;
        location.enabled = false; context.changeWifiState = false;
        check(WifiScanAccess.allowed(context, 12345, 222, "valid.package", null, true), "settings exemption");
        denied = false;
        try { WifiScanAccess.allowed(context, 12345, 222, "spoof", null, false); }
        catch (SecurityException expected) { denied = true; }
        check(denied, "spoofed package");
        context.appOps = null;
        boolean unavailable = false;
        try { WifiScanAccess.allowed(context, 12345, 222, "valid.package", null, false); }
        catch (IllegalStateException expected) { unavailable = true; }
        check(unavailable, "missing app-ops");
        System.out.println("WifiScanAccessTest passed");
    }
}
