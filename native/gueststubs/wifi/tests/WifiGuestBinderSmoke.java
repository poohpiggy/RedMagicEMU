package app.aemu.stub;

import android.content.Context;
import android.content.ContextWrapper;
import android.net.wifi.WifiConfiguration;
import android.os.Binder;
import android.os.Bundle;
import android.os.IBinder;
import android.os.Looper;
import android.os.Parcel;
import android.os.Parcelable;

import java.io.File;
import java.io.FileOutputStream;
import java.lang.reflect.Field;
import java.util.List;
import java.util.Properties;

/** Local Parcel diagnostic; it never publishes the service or registers a network. */
public final class WifiGuestBinderSmoke {
    private static final String DESCRIPTOR = "android.net.wifi.IWifiManager";
    private static class TestContext extends ContextWrapper {
        boolean allow;
        boolean ordinary;
        boolean fine;
        Boolean settingsAllowed;
        Boolean setupWizardAllowed;
        TestContext(Context base) { super(base); }
        @Override public void enforceCallingPermission(String permission, String message) {
            if (!allow) throw new SecurityException("diagnostic denial");
        }
        @Override public int checkPermission(String permission, int pid, int uid) {
            if (settingsAllowed != null && "android.permission.NETWORK_SETTINGS".equals(permission))
                return settingsAllowed ? 0 : -1;
            if (setupWizardAllowed != null && "android.permission.NETWORK_SETUP_WIZARD".equals(permission))
                return setupWizardAllowed ? 0 : -1;
            if (!ordinary) return super.checkPermission(permission, pid, uid);
            if ("android.permission.ACCESS_FINE_LOCATION".equals(permission)) return fine ? 0 : -1;
            return -1;
        }
    }
    private static class Action extends Binder {
        int success;
        int failure;
        @Override protected boolean onTransact(int code, Parcel data, Parcel reply, int flags) {
            data.enforceInterface("android.net.wifi.IActionListener");
            if (code == transaction("android.net.wifi.IActionListener$Stub", "onSuccess")) { success++; return true; }
            if (code == transaction("android.net.wifi.IActionListener$Stub", "onFailure")) { data.readInt(); failure++; return true; }
            return false;
        }
    }
    private static int transaction(String type, String method) {
        try {
            Field field = Class.forName(type).getDeclaredField("TRANSACTION_" + method);
            field.setAccessible(true);
            return field.getInt(null);
        } catch (ReflectiveOperationException failure) { throw new IllegalStateException(failure); }
    }
    private static int wifiTransaction(String method) { return transaction(DESCRIPTOR + "$Stub", method); }
    private static void check(boolean condition, String message) { if (!condition) throw new AssertionError(message); }
    private static Parcel data() { Parcel p = Parcel.obtain(); p.writeInterfaceToken(DESCRIPTOR); return p; }
    private static void position(Parcel p) { p.setDataPosition(0); }

    /** Target-ROM Binder signature used by WifiManager's constructor. */
    private static void verboseLoggingContract(WifiService service) throws Exception {
        java.lang.reflect.Method method = Class.forName(DESCRIPTOR).getMethod("getVerboseLoggingLevel");
        check(method.getReturnType() == int.class && method.getParameterTypes().length == 0,
                "target verbose logging signature");
        Parcel request = data(), reply = Parcel.obtain();
        try {
            position(request);
            check(service.onTransact(wifiTransaction("getVerboseLoggingLevel"), request, reply, 0),
                    "verbose logging query rejected");
            position(reply);
            reply.readException();
            check(reply.readInt() == 0, "virtual verbose logging must be disabled");
            reply.enforceNoDataAvail();
        } finally { request.recycle(); reply.recycle(); }
        request = Parcel.obtain(); reply = Parcel.obtain();
        try {
            request.writeInterfaceToken("app.redmagicemu.invalid");
            position(request);
            boolean rejected = false;
            try { service.onTransact(wifiTransaction("getVerboseLoggingLevel"), request, reply, 0); }
            catch (SecurityException expected) { rejected = true; }
            check(rejected, "verbose logging query rejects wrong token");
        } finally { request.recycle(); reply.recycle(); }
        request = data(); reply = Parcel.obtain();
        try {
            request.writeInt(99);
            position(request);
            boolean rejected = false;
            try { service.onTransact(wifiTransaction("getVerboseLoggingLevel"), request, reply, 0); }
            catch (RuntimeException expected) { rejected = true; }
            check(rejected, "verbose logging query rejects trailing data");
        } finally { request.recycle(); reply.recycle(); }
        request = data();
        try {
            position(request);
            check(!service.onTransact(wifiTransaction("getVerboseLoggingLevel"), request, null, 0),
                    "verbose logging query requires a reply");
        } finally { request.recycle(); }
        System.out.println("WIFI_VERBOSE_LOGGING_CONTRACT_OK");
    }

    /** Compares the prototype against the target ROM's measured original Wi-Fi service. */
    private static void signalLevelContract(WifiService service) throws Exception {
        java.lang.reflect.Method method = Class.forName(DESCRIPTOR).getMethod("calculateSignalLevel", int.class);
        check(method.getReturnType() == int.class, "target signal level signature");
        // NX809J original service measured transitions at -90, -85, -75, -65 dBm.
        int[] samples = { Integer.MIN_VALUE, -160, -100, -91, -90, -86, -85,
                -76, -75, -66, -65, -45, 20, Integer.MAX_VALUE };
        int[] expected = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 4, 4 };
        int previous = -1;
        for (int i = 0; i < samples.length; i++) {
            Parcel request = data(), reply = Parcel.obtain();
            try {
                request.writeInt(samples[i]);
                position(request);
                check(service.onTransact(wifiTransaction("calculateSignalLevel"), request, reply, 0),
                        "signal level transaction not implemented");
                position(reply);
                reply.readException();
                int actual = reply.readInt();
                check(actual == expected[i], "signal level differs from original for " + samples[i]);
                check(actual >= previous, "signal levels must be monotonic");
                previous = actual;
            } finally { request.recycle(); reply.recycle(); }
        }
        Parcel request = data(), reply = Parcel.obtain();
        try {
            position(request);
            boolean rejected = false;
            try { rejected = !service.onTransact(wifiTransaction("calculateSignalLevel"), request, reply, 0); }
            catch (RuntimeException expectedFailure) { rejected = true; }
            check(rejected, "signal level rejects missing RSSI");
        } finally { request.recycle(); reply.recycle(); }
        request = Parcel.obtain(); reply = Parcel.obtain();
        try {
            request.writeInterfaceToken("app.redmagicemu.invalid");
            request.writeInt(-45);
            position(request);
            boolean rejected = false;
            try { rejected = !service.onTransact(wifiTransaction("calculateSignalLevel"), request, reply, 0); }
            catch (SecurityException expectedFailure) { rejected = true; }
            check(rejected, "signal level rejects wrong interface token");
        } finally { request.recycle(); reply.recycle(); }
        request = data(); reply = Parcel.obtain();
        try {
            request.writeInt(-45);
            request.writeInt(99);
            position(request);
            boolean rejected = false;
            try { rejected = !service.onTransact(wifiTransaction("calculateSignalLevel"), request, reply, 0); }
            catch (RuntimeException expectedFailure) { rejected = true; }
            check(rejected, "signal level rejects trailing parcel data");
        } finally { request.recycle(); reply.recycle(); }
        System.out.println("WIFI_SIGNAL_LEVEL_CONTRACT_OK");
    }

    public static void main(String[] args) throws Exception {
        Looper.prepare();
        TestContext context = new TestContext(WifiSystemContext.obtain());
        File temp = File.createTempFile("rme-wifi-binder-", ".properties", new File("/data/local/tmp"));
        try {
            Properties p = new Properties();
            try (FileOutputStream output = new FileOutputStream(temp)) { p.store(output, null); }
            WifiService service = new WifiService(WifiConfig.load(temp), Looper.myLooper(), context);
            if (args.length == 1 && "--verbose-level-contract".equals(args[0])) {
                // TestContext denies calling permissions: the query requires none.
                verboseLoggingContract(service);
                return;
            }
            if (args.length == 1 && "--signal-level-contract".equals(args[0])) {
                // TestContext denies calling and privileged permission checks.
                context.ordinary = true;
                context.settingsAllowed = false;
                context.setupWizardAllowed = false;
                signalLevelContract(service);
                return;
            }
            Parcel request = data(), reply = Parcel.obtain();
            position(request);
            boolean denied = false;
            try { service.onTransact(wifiTransaction("getWifiEnabledState"), request, reply, 0); }
            catch (SecurityException expected) { denied = true; }
            check(denied, "state permission denial");
            request.recycle(); reply.recycle();
            request = data(); reply = Parcel.obtain(); position(request);
            denied = false;
            try { service.onTransact(wifiTransaction("isScanAlwaysAvailable"), request, reply, 0); }
            catch (SecurityException expected) { denied = true; }
            check(denied, "always-available scan permission denial");
            request.recycle(); reply.recycle();
            context.allow = true;
            request = data(); reply = Parcel.obtain(); position(request);
            check(service.onTransact(wifiTransaction("getWifiEnabledState"), request, reply, 0), "state transaction");
            position(reply); reply.readException(); check(reply.readInt() == 3, "enabled state");
            request.recycle(); reply.recycle();
            request = data(); reply = Parcel.obtain(); position(request);
            check(service.onTransact(wifiTransaction("isScanAlwaysAvailable"), request, reply, 0), "always-available scan transaction");
            position(reply); reply.readException(); check(reply.readInt() == 0, "background scan unsupported");
            request.recycle(); reply.recycle();

            // Passpoint is not emulated. Android 16 requires a non-null empty
            // list for a correctly attributed caller, including an ordinary app.
            String passpointMethod = "getPasspointConfigurations";
            java.lang.reflect.Method passpoint = Class.forName(DESCRIPTOR).getMethod(passpointMethod, String.class);
            Class<?> passpointSlice = passpoint.getReturnType();
            Parcelable.Creator<?> passpointCreator =
                    (Parcelable.Creator<?>) passpointSlice.getField("CREATOR").get(null);
            context.settingsAllowed = false;
            context.setupWizardAllowed = false;
            context.allow = false;
            request = data(); reply = Parcel.obtain(); request.writeString("root"); position(request);
            check(service.onTransact(wifiTransaction(passpointMethod), request, reply, 0),
                    "passpoint empty-list transaction");
            position(reply); reply.readException(); check(reply.readInt() == 1, "passpoint result non-null");
            Object passpointResult = passpointCreator.createFromParcel(reply);
            check(((List<?>) passpointSlice.getMethod("getList").invoke(passpointResult)).isEmpty(),
                    "no configured Passpoint networks");
            request.recycle(); reply.recycle();

            request = data(); reply = Parcel.obtain();
            request.writeString("app.redmagicemu.invalid.unowned"); position(request);
            boolean passpointDenied = false;
            try { service.onTransact(wifiTransaction(passpointMethod), request, reply, 0); }
            catch (SecurityException expected) { passpointDenied = true; }
            check(passpointDenied, "passpoint rejects unowned package");
            request.recycle(); reply.recycle();

            request = data(); reply = Parcel.obtain(); request.writeString("root"); request.writeInt(42); position(request);
            boolean passpointTrailingRejected = false;
            try { service.onTransact(wifiTransaction(passpointMethod), request, reply, 0); }
            catch (RuntimeException expected) { passpointTrailingRejected = true; }
            check(passpointTrailingRejected, "passpoint rejects trailing parcel data");
            request.recycle(); reply.recycle();
            context.allow = true;
            context.settingsAllowed = null;
            context.setupWizardAllowed = null;

            String suggestionsMethod = "getWifiConfigForMatchedNetworkSuggestionsSharedWithUser";
            java.lang.reflect.Method suggestions = null;
            for (java.lang.reflect.Method m : Class.forName(DESCRIPTOR).getMethods())
                if (suggestionsMethod.equals(m.getName())) suggestions = m;
            check(suggestions != null && suggestions.getParameterTypes().length == 1,
                    "target suggestions method signature");
            Class<?> suggestionSlice = suggestions.getParameterTypes()[0];
            check(suggestions.getReturnType().equals(suggestionSlice), "suggestion list return type");
            Parcelable emptyScanSlice = (Parcelable) suggestionSlice.getConstructor(List.class)
                    .newInstance(new java.util.ArrayList<Parcelable>());
            java.util.ArrayList<Parcelable> oneScan = new java.util.ArrayList<Parcelable>();
            oneScan.add(new android.net.wifi.ScanResult());
            Parcelable populatedScanSlice = (Parcelable) suggestionSlice.getConstructor(List.class).newInstance(oneScan);
            Parcelable.Creator<?> suggestionCreator = (Parcelable.Creator<?>) suggestionSlice.getField("CREATOR").get(null);

            context.settingsAllowed = false;
            context.setupWizardAllowed = false;
            request = data(); reply = Parcel.obtain(); request.writeTypedObject(null, 0); position(request);
            denied = false;
            try { service.onTransact(wifiTransaction(suggestionsMethod), request, reply, 0); }
            catch (SecurityException expected) { denied = true; }
            check(denied, "suggestion matching requires settings privilege");
            request.recycle(); reply.recycle();

            context.settingsAllowed = true;
            for (Parcelable scans : new Parcelable[] { null, emptyScanSlice, populatedScanSlice }) {
                request = data(); reply = Parcel.obtain(); request.writeTypedObject(scans, 0); position(request);
                check(service.onTransact(wifiTransaction(suggestionsMethod), request, reply, 0),
                        "suggestion matching transaction");
                position(reply); reply.readException(); check(reply.readInt() == 1, "suggestion result non-null");
                Object result = suggestionCreator.createFromParcel(reply);
                check(((List<?>) suggestionSlice.getMethod("getList").invoke(result)).isEmpty(),
                        "no configured suggestions produces empty list");
                request.recycle(); reply.recycle();
            }
            context.settingsAllowed = false;
            context.setupWizardAllowed = true;
            request = data(); reply = Parcel.obtain(); request.writeTypedObject(null, 0); position(request);
            check(service.onTransact(wifiTransaction(suggestionsMethod), request, reply, 0),
                    "setup wizard suggestion transaction");
            position(reply); reply.readException(); check(reply.readInt() == 1, "setup wizard result non-null");
            Object setupResult = suggestionCreator.createFromParcel(reply);
            check(((List<?>) suggestionSlice.getMethod("getList").invoke(setupResult)).isEmpty(),
                    "setup wizard sees no suggestions");
            request.recycle(); reply.recycle();
            request = data(); reply = Parcel.obtain(); request.writeTypedObject(null, 0); request.writeInt(42); position(request);
            boolean extraRejected = false;
            try { service.onTransact(wifiTransaction(suggestionsMethod), request, reply, 0); }
            catch (RuntimeException expected) { extraRejected = true; }
            check(extraRejected, "suggestion matching rejects trailing parcel data");
            request.recycle(); reply.recycle();
            context.settingsAllowed = null;
            context.setupWizardAllowed = null;

            // Android 16 declares forget() oneway: the service receives no reply parcel.
            Action unknownNetwork = new Action();
            request = data();
            request.writeInt(-1); request.writeStrongBinder(unknownNetwork); position(request);
            check(service.onTransact(wifiTransaction("forget"), request, null, IBinder.FLAG_ONEWAY),
                    "oneway forget transaction");
            check(unknownNetwork.failure == 1 && unknownNetwork.success == 0,
                    "unknown network forget reports failure through listener");
            request.recycle();

            context.allow = false;
            Action deniedForget = new Action();
            request = data();
            request.writeInt(0); request.writeStrongBinder(deniedForget); position(request);
            denied = false;
            try { service.onTransact(wifiTransaction("forget"), request, null, IBinder.FLAG_ONEWAY); }
            catch (SecurityException expected) { denied = true; }
            check(denied && deniedForget.success == 0 && deniedForget.failure == 0,
                    "oneway forget requires privilege");
            request.recycle();
            context.allow = true;

            request = data(); request.writeInt(-1); request.writeStrongBinder(null); position(request);
            check(!service.onTransact(wifiTransaction("forget"), request, null, 0),
                    "missing synchronous reply rejected");
            request.recycle();

            request = data(); reply = Parcel.obtain();
            request.writeString("root"); request.writeString(null); position(request);
            check(service.onTransact(wifiTransaction("getScanResults"), request, reply, 0), "scan transaction");
            position(reply); reply.readException(); check(reply.readInt() == 1, "scan slice present");
            Class<?> sliceType = Class.forName(DESCRIPTOR).getMethod("getScanResults", String.class, String.class).getReturnType();
            Parcelable.Creator<?> creator = (Parcelable.Creator<?>) sliceType.getField("CREATOR").get(null);
            Object slice = creator.createFromParcel(reply);
            List<?> results = (List<?>) sliceType.getMethod("getList").invoke(slice);
            check(results.size() == 1, "one virtual scan result");
            request.recycle(); reply.recycle();

            context.ordinary = true;
            context.fine = false;
            request = data(); reply = Parcel.obtain();
            request.writeString("root"); request.writeString(null); position(request);
            check(service.onTransact(wifiTransaction("getScanResults"), request, reply, 0), "ordinary scan without location");
            position(reply); reply.readException(); check(reply.readInt() == 0, "location denied returns null");
            request.recycle(); reply.recycle();
            request = data(); reply = Parcel.obtain();
            request.writeString("root"); request.writeString(null); position(request);
            check(service.onTransact(wifiTransaction("startScan"), request, reply, 0), "ordinary start without location");
            position(reply); reply.readException(); check(reply.readInt() == 0, "location denied refuses start");
            request.recycle(); reply.recycle();
            context.fine = true;
            boolean access = WifiScanAccess.allowed(context, Binder.getCallingUid(), Binder.getCallingPid(), "root", null, false);
            request = data(); reply = Parcel.obtain();
            request.writeString("root"); request.writeString(null); position(request);
            check(service.onTransact(wifiTransaction("getScanResults"), request, reply, 0), "ordinary scan with location permission");
            position(reply); reply.readException();
            check(reply.readInt() == (access ? 1 : 0), "guest location/AppOps result respected");
            System.out.println("WIFI_GUEST_SCAN_ACCESS location_and_appops=" + (access ? "allowed" : "denied"));
            request.recycle(); reply.recycle();
            context.ordinary = false;

            WifiConfiguration wrong = new WifiConfiguration();
            wrong.SSID = "\"RedMagicEMU\"";
            wrong.preSharedKey = "\"wrong-password\"";
            Action callback = new Action();
            request = data(); reply = Parcel.obtain();
            request.writeTypedObject(wrong, 0); request.writeInt(0); request.writeStrongBinder(callback);
            request.writeString("root"); request.writeTypedObject(new Bundle(), 0); position(request);
            check(service.onTransact(wifiTransaction("connect"), request, reply, 0), "connect transaction");
            position(reply); reply.readException();
            check(callback.failure == 1 && callback.success == 0, "wrong password callback");
            request.recycle(); reply.recycle();

            Action wrongOneWay = new Action();
            request = data();
            request.writeTypedObject(wrong, 0); request.writeInt(0); request.writeStrongBinder(wrongOneWay);
            request.writeString("root"); request.writeTypedObject(new Bundle(), 0); position(request);
            check(service.onTransact(wifiTransaction("connect"), request, null, IBinder.FLAG_ONEWAY),
                    "oneway connect transaction");
            check(wrongOneWay.failure == 1 && wrongOneWay.success == 0,
                    "oneway wrong credentials report failure");
            request.recycle();

            context.allow = false;
            Action deniedConnect = new Action();
            request = data();
            request.writeTypedObject(wrong, 0); request.writeInt(0); request.writeStrongBinder(deniedConnect);
            request.writeString("root"); request.writeTypedObject(new Bundle(), 0); position(request);
            denied = false;
            try { service.onTransact(wifiTransaction("connect"), request, null, IBinder.FLAG_ONEWAY); }
            catch (SecurityException expected) { denied = true; }
            check(denied && deniedConnect.success == 0 && deniedConnect.failure == 0,
                    "oneway connect requires privilege");
            request.recycle();
            context.allow = true;

            request = data();
            request.writeTypedObject(wrong, 0); request.writeInt(0); request.writeStrongBinder(null);
            request.writeString("root"); request.writeTypedObject(new Bundle(), 0); position(request);
            check(!service.onTransact(wifiTransaction("connect"), request, null, 0),
                    "connect missing synchronous reply rejected");
            request.recycle();

            WifiConfiguration open = new WifiConfiguration();
            open.SSID = "\"RedMagicEMU\"";
            Action noNetworkCallback = new Action();
            request = data(); reply = Parcel.obtain();
            request.writeTypedObject(open, 0); request.writeInt(0); request.writeStrongBinder(noNetworkCallback);
            request.writeString("root"); request.writeTypedObject(new Bundle(), 0); position(request);
            check(service.onTransact(wifiTransaction("connect"), request, reply, 0), "connect without network transaction");
            position(reply); reply.readException();
            System.out.println("WIFI_MISSING_LISTENER_CALLBACK success=" + noNetworkCallback.success + " failure=" + noNetworkCallback.failure);
            check(noNetworkCallback.failure == 1 && noNetworkCallback.success == 0,
                    "missing network listener must not report a successful connection");
            request.recycle(); reply.recycle();

            for (String name : new String[] { "disconnect", "disconnect2", "reconnect" }) {
                request = data(); reply = Parcel.obtain(); request.writeString("root"); position(request);
                check(service.onTransact(wifiTransaction(name), request, reply, 0), name + " transaction");
                position(reply); reply.readException();
                check(reply.readInt() == 0, name + " cannot claim success without network listener");
                request.recycle(); reply.recycle();
            }
            request = data(); reply = Parcel.obtain();
            request.writeInt(0); request.writeBoolean(true); request.writeString("root"); position(request);
            check(service.onTransact(wifiTransaction("enableNetwork"), request, reply, 0), "enableNetwork transaction");
            position(reply); reply.readException(); check(reply.readInt() == 0, "enable without agent rejected");
            request.recycle(); reply.recycle();

            request = data(); reply = Parcel.obtain();
            request.writeString("root"); request.writeString(null); request.writeBoolean(false); position(request);
            check(service.onTransact(wifiTransaction("getConfiguredNetworks"), request, reply, 0), "configured transaction");
            position(reply); reply.readException(); check(reply.readInt() == 1, "configured slice present");
            Class<?> configSlice = Class.forName(DESCRIPTOR).getMethod("getConfiguredNetworks", String.class, String.class, boolean.class).getReturnType();
            Parcelable.Creator<?> configCreator = (Parcelable.Creator<?>) configSlice.getField("CREATOR").get(null);
            Object configured = configCreator.createFromParcel(reply);
            check(((List<?>) configSlice.getMethod("getList").invoke(configured)).size() == 1, "one open saved network");
            request.recycle(); reply.recycle();

            request = data(); reply = Parcel.obtain();
            request.writeInt(0); request.writeString("root"); request.writeString(null); position(request);
            check(service.onTransact(wifiTransaction("getConfiguredNetworks2"), request, reply, 0), "secondary configured transaction");
            position(reply); reply.readException(); check(reply.readInt() == 1, "secondary slice present");
            configured = configCreator.createFromParcel(reply);
            check(((List<?>) configSlice.getMethod("getList").invoke(configured)).isEmpty(), "no secondary network");
            request.recycle(); reply.recycle();

            request = data(); reply = Parcel.obtain(); position(request);
            boolean unavailable = false;
            try { service.onTransact(wifiTransaction("getCurrentNetwork"), request, reply, 0); }
            catch (IllegalStateException expected) { unavailable = true; }
            check(unavailable, "connected service without agent reports unavailable");
            request.recycle(); reply.recycle();

            WifiService connectedService = new WifiService(WifiConfig.load(temp), Looper.myLooper(), context);
            connectedService.setListener(new WifiService.Listener() {
                @Override public boolean onConnectionChanged(boolean connected, Object candidateWifiInfo) {
                    return true;
                }
            });
            Action removed = new Action();
            request = data(); request.writeInt(0); request.writeStrongBinder(removed); position(request);
            check(connectedService.onTransact(wifiTransaction("forget"), request, null, IBinder.FLAG_ONEWAY),
                    "oneway saved network forget transaction");
            check(removed.success == 1 && removed.failure == 0 && !connectedService.isConnected(),
                    "oneway forget disconnects and reports success");
            request.recycle();

            connectedService.setListener(new WifiService.Listener() {
                @Override public boolean onConnectionChanged(boolean connected, Object candidateWifiInfo) {
                    return false;
                }
            });
            Action rejectedConnection = new Action();
            request = data();
            request.writeTypedObject(open, 0); request.writeInt(0); request.writeStrongBinder(rejectedConnection);
            request.writeString("root"); request.writeTypedObject(new Bundle(), 0); position(request);
            check(connectedService.onTransact(wifiTransaction("connect"), request, null, IBinder.FLAG_ONEWAY),
                    "oneway rejected network transaction");
            check(rejectedConnection.failure == 1 && rejectedConnection.success == 0 && !connectedService.isConnected(),
                    "oneway connection failure preserves disconnected state");
            request.recycle();

            connectedService.setListener(new WifiService.Listener() {
                @Override public boolean onConnectionChanged(boolean connected, Object candidateWifiInfo) {
                    return connected && candidateWifiInfo != null;
                }
            });
            Action connected = new Action();
            request = data();
            request.writeTypedObject(open, 0); request.writeInt(0); request.writeStrongBinder(connected);
            request.writeString("root"); request.writeTypedObject(new Bundle(), 0); position(request);
            check(connectedService.onTransact(wifiTransaction("connect"), request, null, IBinder.FLAG_ONEWAY),
                    "oneway open network transaction");
            check(connected.success == 1 && connected.failure == 0 && connectedService.isConnected(),
                    "oneway connect restores state and reports success");
            request.recycle();
            System.out.println("WIFI_GUEST_BINDER_SMOKE_OK");
        } finally {
            if (!temp.delete()) System.err.println("could not remove diagnostic properties");
        }
    }
}
