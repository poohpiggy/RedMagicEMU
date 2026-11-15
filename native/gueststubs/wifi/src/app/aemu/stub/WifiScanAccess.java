package app.aemu.stub;

import android.app.AppOpsManager;
import android.content.Context;
import android.content.pm.PackageManager;
import android.location.LocationManager;
import android.os.Binder;
import android.os.UserHandle;

/** Target API 36 scan authorization; unknown location/user state is denied. */
final class WifiScanAccess {
    static boolean allowed(Context context, int uid, int pid, String packageName,
            String attributionTag, boolean start) {
        AppOpsManager ops = (AppOpsManager) context.getSystemService(Context.APP_OPS_SERVICE);
        if (ops == null) throw new IllegalStateException("guest app-ops unavailable");
        ops.checkPackage(uid, packageName);
        boolean networkSettings = hasPermission(context, "android.permission.NETWORK_SETTINGS", pid, uid);
        if (start && !networkSettings) {
            context.enforceCallingPermission("android.permission.CHANGE_WIFI_STATE", "startScan");
            if (ops.noteOp("android:change_wifi_state", uid, packageName) != AppOpsManager.MODE_ALLOWED) return false;
        }
        if (networkSettings
                || hasPermission(context, "android.permission.NETWORK_SETUP_WIZARD", pid, uid)
                || hasPermission(context, "android.permission.NETWORK_MANAGED_PROVISIONING", pid, uid)
                || hasPermission(context, "android.permission.NETWORK_STACK", pid, uid)
                || hasPermission(context, "android.permission.MAINLINE_NETWORK_STACK", pid, uid)) return true;
        // Conservatively require the same user as the guest service. Cross-user
        // profiles remain redacted until the ROM foreground-profile checks exist.
        if (!UserHandle.getUserHandleForUid(uid).equals(android.os.Process.myUserHandle())
                || !hasPermission(context, "android.permission.ACCESS_FINE_LOCATION", pid, uid)) return false;
        long identity = Binder.clearCallingIdentity();
        try {
            LocationManager location = (LocationManager) context.getSystemService(Context.LOCATION_SERVICE);
            return location != null && location.isLocationEnabled()
                    && ops.noteOpNoThrow("android:fine_location", uid, packageName, attributionTag, null) == AppOpsManager.MODE_ALLOWED
                    && ops.noteOpNoThrow("android:wifi_scan", uid, packageName, attributionTag, null) == AppOpsManager.MODE_ALLOWED;
        } finally { Binder.restoreCallingIdentity(identity); }
    }
    private static boolean hasPermission(Context context, String permission, int pid, int uid) {
        return context.checkPermission(permission, pid, uid) == PackageManager.PERMISSION_GRANTED;
    }
    private WifiScanAccess() { }
}
