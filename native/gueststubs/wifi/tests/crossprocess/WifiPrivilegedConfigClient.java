package app.aemu.stub;

import android.content.AttributionSource;
import android.net.wifi.WifiConfiguration;
import android.os.BadParcelableException;
import android.os.Binder;
import android.os.Bundle;
import android.os.IBinder;
import android.os.Parcel;
import android.os.Parcelable;
import android.os.Process;

import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.util.List;

/** Exercises real Binder IPC using the process's actual UID and permissions. */
public final class WifiPrivilegedConfigClient {
    private static final String DESCRIPTOR = "android.net.wifi.IWifiManager";
    private static final String ATTRIBUTION = "EXTRA_PARAM_KEY_ATTRIBUTION_SOURCE";
    private static void check(boolean value, String message) { if (!value) throw new AssertionError(message); }
    private static int code(String method) throws Exception {
        Field field = Class.forName(DESCRIPTOR + "$Stub").getDeclaredField("TRANSACTION_" + method);
        field.setAccessible(true);
        return field.getInt(null);
    }
    private static Bundle source(int uid, String name, AttributionSource next) {
        AttributionSource.Builder builder = new AttributionSource.Builder(uid).setPackageName(name);
        if (next != null) builder.setNext(next);
        Bundle extras = new Bundle();
        extras.putParcelable(ATTRIBUTION, builder.build());
        return extras;
    }
    private static List<?> query(IBinder remote, Method method, String name, Bundle extras,
            boolean trailing) throws Exception {
        Parcel data = Parcel.obtain(), reply = Parcel.obtain();
        try {
            data.writeInterfaceToken(DESCRIPTOR);
            data.writeString(name);
            data.writeString(null);
            data.writeTypedObject(extras, 0);
            if (trailing) data.writeInt(42);
            check(remote.transact(code(method.getName()), data, reply, 0), "transaction unimplemented");
            reply.readException();
            if (reply.readInt() == 0) return null;
            Class<?> type = method.getReturnType();
            Parcelable.Creator<?> creator = (Parcelable.Creator<?>) type.getField("CREATOR").get(null);
            return (List<?>) type.getMethod("getList").invoke(creator.createFromParcel(reply));
        } finally { data.recycle(); reply.recycle(); }
    }
    private static void denied(IBinder service, Method method, String name, Bundle extras,
            boolean trailing, String message, Class<? extends RuntimeException> expectedType,
            String expectedReason) throws Exception {
        try {
            query(service, method, name, extras, trailing);
        } catch (RuntimeException failure) {
            if (expectedType.isInstance(failure) && failure.getMessage() != null
                    && failure.getMessage().contains(expectedReason)) return;
            throw new AssertionError(message + ": unexpected failure", failure);
        }
        throw new AssertionError(message + ": no exception");
    }
    public static void main(String[] args) throws Exception {
        if (args.length != 4 || !args[0].startsWith("redmagicemu.test.wifi."))
            throw new IllegalArgumentException("service-name package actual-uid authorized|denied");
        int uid = Process.myUid();
        check(uid == Integer.parseInt(args[2]), "actual UID differs from expected UID");
        check("authorized".equals(args[3]) || "denied".equals(args[3]), "invalid test mode");
        Class<?> manager = Class.forName("android.os.ServiceManager");
        IBinder service = (IBinder) manager.getMethod("checkService", String.class).invoke(null, args[0]);
        check(service != null && service.isBinderAlive() && !(service instanceof Binder),
                "endpoint unavailable or not a remote Binder proxy");
        System.out.println("WIFI_PRIVILEGED_CROSS_CLIENT uid=" + uid + " pid=" + Process.myPid());
        Method method = Class.forName(DESCRIPTOR).getMethod("getPrivilegedConfiguredNetworks",
                String.class, String.class, Bundle.class);
        check(method.getReturnType().getName().endsWith("ParceledListSlice"), "target signature differs");
        Bundle valid = source(uid, args[1], null);
        if ("denied".equals(args[3])) {
            denied(service, method, args[1], valid, false, "unprivileged caller received config",
                    SecurityException.class, "android.permission.READ_WIFI_CREDENTIAL");
            System.out.println("WIFI_PRIVILEGED_CROSS_DENIED_OK");
            return;
        }
        List<?> networks = query(service, method, args[1], valid, false);
        check(networks != null && networks.size() == 1, "authorized caller did not receive one network");
        WifiConfiguration network = (WifiConfiguration) networks.get(0);
        check(network.networkId == 0 && "\"RedMagicEMU\"".equals(network.SSID), "network identity");
        check(network.allowedKeyManagement.cardinality() == 1
                && network.allowedKeyManagement.get(WifiConfiguration.KeyMgmt.NONE)
                && network.preSharedKey == null, "network security");
        denied(service, method, "app.redmagicemu.invalid.unowned", valid, false, "forged caller package",
                SecurityException.class, "Package app.redmagicemu.invalid.unowned does not belong to ");
        denied(service, method, args[1], null, false, "missing attribution",
                SecurityException.class, "invalid privileged configuration attribution");
        denied(service, method, args[1], source(uid + 1, args[1], null), false, "forged source UID",
                SecurityException.class, "invalid privileged configuration attribution");
        denied(service, method, args[1], source(uid, "app.redmagicemu.invalid.unowned", null), false,
                "forged source package", SecurityException.class, "invalid privileged configuration attribution");
        AttributionSource next = new AttributionSource.Builder(uid + 1)
                .setPackageName("app.redmagicemu.invalid.unowned").build();
        denied(service, method, args[1], source(uid, args[1], next), false, "untrusted attribution chain",
                SecurityException.class, "invalid privileged configuration attribution");
        denied(service, method, args[1], valid, true, "trailing data", BadParcelableException.class,
                "Parcel data not fully consumed");
        Parcel data = Parcel.obtain();
        try {
            data.writeInterfaceToken(DESCRIPTOR);
            data.writeInt(0);
            data.writeStrongBinder(null);
            check(service.transact(code("forget"), data, null, IBinder.FLAG_ONEWAY), "forget unavailable");
        } finally { data.recycle(); }
        long deadline = android.os.SystemClock.elapsedRealtime() + 5000;
        do {
            networks = query(service, method, args[1], valid, false);
            if (networks != null && networks.isEmpty()) {
                System.out.println("WIFI_PRIVILEGED_CROSS_AUTHORIZED_OK");
                return;
            }
            Thread.sleep(100);
        } while (android.os.SystemClock.elapsedRealtime() < deadline);
        throw new AssertionError("forgotten network remained saved");
    }
}
