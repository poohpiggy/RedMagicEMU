package android.content;

public class Context {
    public final java.util.List<Intent> broadcasts = new java.util.ArrayList<Intent>();
    public void sendBroadcast(Intent intent) { broadcasts.add(intent); }
    public String lastBroadcastPermission;
    public void sendBroadcast(Intent intent,String permission) { lastBroadcastPermission=permission; sendBroadcast(intent); }
    public boolean allowed;
    public boolean networkSettings;
    public boolean networkSetupWizard;
    public boolean changeWifiState;
    public boolean fineLocation;
    public Object appOps;
    public Object location;
    public static final String APP_OPS_SERVICE = "appops";
    public static final String LOCATION_SERVICE = "location";
    public Object getSystemService(String name) { return APP_OPS_SERVICE.equals(name) ? appOps : LOCATION_SERVICE.equals(name) ? location : null; }
    public int checkPermission(String permission, int pid, int uid) {
        return ("android.permission.NETWORK_SETTINGS".equals(permission) && networkSettings)
                || ("android.permission.NETWORK_SETUP_WIZARD".equals(permission) && networkSetupWizard)
                || ("android.permission.ACCESS_FINE_LOCATION".equals(permission) && fineLocation) ? 0 : -1;
    }
    public void enforceCallingPermission(String permission, String message) {
        if ("android.permission.ACCESS_WIFI_STATE".equals(permission) && allowed) return;
        if ("android.permission.CHANGE_WIFI_STATE".equals(permission) && changeWifiState) return;
        throw new SecurityException();
    }
}
