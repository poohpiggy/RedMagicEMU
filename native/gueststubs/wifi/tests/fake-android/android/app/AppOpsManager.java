package android.app;
public class AppOpsManager {
    public static final int MODE_ALLOWED = 0;
    public int mode;
    public int notes;
    public int fineMode, scanMode;
    public int authorizedUid = 12345;
    public String authorizedPackage = "valid.package";
    public void checkPackage(int uid, String name) {
        if (uid != authorizedUid || !authorizedPackage.equals(name)) throw new SecurityException();
    }
    public int noteOp(String operation, int uid, String name) {
        if (!"android:change_wifi_state".equals(operation) || uid != 12345
                || !authorizedPackage.equals(name)) throw new AssertionError("incorrect app-op caller");
        notes++;
        if (mode == 2) throw new SecurityException();
        return mode;
    }
    public int noteOpNoThrow(String operation, int uid, String name, String attribution, String message) {
        checkPackage(uid, name);
        if ("android:fine_location".equals(operation)) return fineMode;
        if ("android:wifi_scan".equals(operation)) return scanMode;
        throw new AssertionError("unexpected location app-op");
    }
}
