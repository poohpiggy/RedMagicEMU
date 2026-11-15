package app.aemu.stub;

import android.content.AttributionSource;
import android.content.Context;
import android.content.ContextWrapper;
import android.net.wifi.WifiConfiguration;
import android.os.Binder;
import android.os.BadParcelableException;
import android.os.Bundle;
import android.os.IBinder;
import android.os.Looper;
import android.os.Parcel;
import android.os.Parcelable;

import java.io.File;
import java.io.FileOutputStream;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.util.List;
import java.util.Properties;

/** Local-only diagnostic: API 36 rejects unparceling AttributionSource without real Binder IPC. */
public final class WifiPrivilegedConfigBinderTest {
    private static final String DESCRIPTOR = "android.net.wifi.IWifiManager";
    private static final String ATTRIBUTION = "EXTRA_PARAM_KEY_ATTRIBUTION_SOURCE";
    private static final String CREDENTIAL = "android.permission.READ_WIFI_CREDENTIAL";
    private static final String ACCESS = "android.permission.ACCESS_WIFI_STATE";
    private static final String SETTINGS = "android.permission.NETWORK_SETTINGS";
    private static final String SUW = "android.permission.NETWORK_SETUP_WIZARD";

    private static final class TestContext extends ContextWrapper {
        String denied;
        boolean settings = true;
        boolean setupWizard;
        TestContext(Context base) { super(base); }
        @Override public void enforceCallingPermission(String permission, String message) {
            if (permission.equals(denied)) throw new SecurityException("test denies " + permission);
        }
        @Override public void enforceCallingOrSelfPermission(String permission, String message) {
            if (permission.equals(denied)) throw new SecurityException("test denies " + permission);
        }
        @Override public int checkPermission(String permission, int pid, int uid) {
            if (permission.equals(denied)) return -1;
            if (permission.equals(SETTINGS)) return settings ? 0 : -1;
            if (permission.equals(SUW)) return setupWizard ? 0 : -1;
            return super.checkPermission(permission, pid, uid);
        }
    }

    private static void check(boolean value, String message) {
        if (!value) throw new AssertionError(message);
    }

    private static int transaction(String name) throws Exception {
        Field f = Class.forName(DESCRIPTOR + "$Stub").getDeclaredField("TRANSACTION_" + name);
        f.setAccessible(true);
        return f.getInt(null);
    }

    private static Bundle attribution(int uid, String packageName) {
        Bundle extras = new Bundle();
        extras.putParcelable(ATTRIBUTION,
                new AttributionSource.Builder(uid).setPackageName(packageName).build());
        return extras;
    }

    private static List<?> query(WifiService service, Method method, String packageName,
            Bundle extras, boolean trailing) throws Exception {
        Parcel request = Parcel.obtain(), reply = Parcel.obtain();
        try {
            request.writeInterfaceToken(DESCRIPTOR);
            request.writeString(packageName);
            request.writeString(null);
            request.writeTypedObject(extras, 0);
            if (trailing) request.writeInt(42);
            request.setDataPosition(0);
            check(service.onTransact(transaction(method.getName()), request, reply, 0),
                    "getPrivilegedConfiguredNetworks transaction rejected");
            reply.setDataPosition(0);
            reply.readException();
            if (reply.readInt() == 0) return null;
            Parcelable.Creator<?> creator = (Parcelable.Creator<?>) method.getReturnType()
                    .getField("CREATOR").get(null);
            Object slice = creator.createFromParcel(reply);
            return (List<?>) method.getReturnType().getMethod("getList").invoke(slice);
        } finally {
            request.recycle();
            reply.recycle();
        }
    }

    private static void rejects(WifiService service, Method method, String name,
            Bundle extras, boolean trailing, String message) throws Exception {
        try {
            List<?> result = query(service, method, name, extras, trailing);
            check(result == null, message);
        } catch (SecurityException | IllegalArgumentException | BadParcelableException expected) {
            // Both an exception and a null result deny access to the configuration.
        }
    }

    public static void main(String[] args) throws Exception {
        check(args.length == 1, "provide a package owned by the current Settings UID");
        String packageName = args[0];
        Looper.prepare();
        TestContext context = new TestContext(WifiSystemContext.obtain());
        File file = File.createTempFile("rme-privileged-config-", ".properties", new File("/data/local/tmp"));
        try {
            try (FileOutputStream out = new FileOutputStream(file)) { new Properties().store(out, null); }
            WifiService service = new WifiService(WifiConfig.load(file), Looper.myLooper(), context);
            service.setListener(new WifiService.Listener() {
                @Override public boolean onConnectionChanged(boolean connected, Object info) { return true; }
            });
            Method method = Class.forName(DESCRIPTOR).getMethod("getPrivilegedConfiguredNetworks",
                    String.class, String.class, Bundle.class);
            check(method.getReturnType().getName().endsWith("ParceledListSlice"), "target result signature");
            Bundle valid = attribution(Binder.getCallingUid(), packageName);
            List<?> configs = query(service, method, packageName, valid, false);
            check(configs != null && configs.size() == 1, "Settings receives one saved network");
            WifiConfiguration config = (WifiConfiguration) configs.get(0);
            check(config.networkId == 0 && "\"RedMagicEMU\"".equals(config.SSID), "network identity");
            check(config.allowedKeyManagement.get(WifiConfiguration.KeyMgmt.NONE)
                    && config.allowedKeyManagement.cardinality() == 1 && config.preSharedKey == null,
                    "network is open");
            context.settings = false;
            context.setupWizard = true;
            check(query(service, method, packageName, valid, false) != null, "setup wizard path");
            context.setupWizard = false;
            rejects(service, method, packageName, valid, false, "unaudited caller rejected");
            context.settings = true;
            context.denied = CREDENTIAL;
            rejects(service, method, packageName, valid, false, "credentials permission required");
            context.denied = ACCESS;
            rejects(service, method, packageName, valid, false, "Wi-Fi state permission required");
            context.denied = null;
            rejects(service, method, "app.redmagicemu.invalid.unowned", valid, false, "forged package rejected");
            rejects(service, method, packageName, null, false, "missing attribution rejected");
            rejects(service, method, packageName, attribution(Binder.getCallingUid() + 1, packageName),
                    false, "forged attribution UID rejected");
            rejects(service, method, packageName, attribution(Binder.getCallingUid(), "app.redmagicemu.invalid.unowned"),
                    false, "forged attribution package rejected");
            AttributionSource forgedNext = new AttributionSource.Builder(Binder.getCallingUid() + 1)
                    .setPackageName("app.redmagicemu.invalid.unowned").build();
            Bundle chain = new Bundle();
            chain.putParcelable(ATTRIBUTION, new AttributionSource.Builder(Binder.getCallingUid())
                    .setPackageName(packageName).setNext(forgedNext).build());
            rejects(service, method, packageName, chain, false, "untrusted attribution chain rejected");
            rejects(service, method, packageName, valid, true, "trailing data rejected");
            Parcel request = Parcel.obtain();
            try {
                request.writeInterfaceToken(DESCRIPTOR);
                request.writeInt(0);
                request.writeStrongBinder(null);
                request.setDataPosition(0);
                check(service.onTransact(transaction("forget"), request, null, IBinder.FLAG_ONEWAY),
                        "forget transaction");
            } finally { request.recycle(); }
            configs = query(service, method, packageName, valid, false);
            check(configs != null && configs.isEmpty(), "forgotten network absent from configurations");
            System.out.println("WIFI_PRIVILEGED_CONFIG_BINDER_TEST_OK");
        } finally { if (!file.delete()) System.err.println("could not remove test properties"); }
    }
}
