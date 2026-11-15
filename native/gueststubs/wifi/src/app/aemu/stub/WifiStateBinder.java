package app.aemu.stub;

import android.content.Context;
import android.content.pm.PackageManager;
import android.app.AppOpsManager;
import android.net.Network;
import android.os.Binder;
import android.os.IBinder;
import android.os.Parcel;
import android.os.RemoteException;

import java.lang.reflect.Field;

/**
 * Experimental API 36 Binder endpoint for the Wi-Fi enabled-state query and
 * caller validation for setWifiEnabled. Toggle requests always return false:
 * platform restrictions and transition/callback integration are incomplete.
 * It must not be registered as the guest's Wi-Fi service until the remaining
 * APIs, lifecycle, caller checks, and integration have been implemented.
 */
final class WifiStateBinder extends Binder {
    private static final String DESCRIPTOR = "android.net.wifi.IWifiManager";
    private static final String ACCESS_WIFI_STATE = "android.permission.ACCESS_WIFI_STATE";
    private static final String CHANGE_WIFI_STATE = "android.permission.CHANGE_WIFI_STATE";
    private static final String NETWORK_SETTINGS = "android.permission.NETWORK_SETTINGS";
    private static final String NETWORK_SETUP_WIZARD = "android.permission.NETWORK_SETUP_WIZARD";
    private static final String OP_CHANGE_WIFI_STATE = "android:change_wifi_state";
    private final Context context;
    private final VirtualWifiState state;
    private final int getWifiEnabledStateTransaction;
    private final int setWifiEnabledTransaction;
    private final int getCurrentNetworkTransaction;
    private final CurrentNetworkSource networkSource;

    interface CurrentNetworkSource { Network get(); }

    WifiStateBinder(Context context, VirtualWifiState state) throws ReflectiveOperationException {
        this(context, state, null);
    }

    WifiStateBinder(Context context, VirtualWifiState state, CurrentNetworkSource networkSource)
            throws ReflectiveOperationException {
        if (context == null || state == null) throw new IllegalArgumentException("context and state required");
        this.context = context;
        this.state = state;
        this.networkSource = networkSource;
        Field transaction = Class.forName(DESCRIPTOR + "$Stub")
                .getDeclaredField("TRANSACTION_getWifiEnabledState");
        transaction.setAccessible(true);
        getWifiEnabledStateTransaction = transaction.getInt(null);
        Field toggle = Class.forName(DESCRIPTOR + "$Stub")
                .getDeclaredField("TRANSACTION_setWifiEnabled");
        toggle.setAccessible(true);
        setWifiEnabledTransaction = toggle.getInt(null);
        Field current = Class.forName(DESCRIPTOR + "$Stub").getDeclaredField("TRANSACTION_getCurrentNetwork");
        current.setAccessible(true);
        getCurrentNetworkTransaction = current.getInt(null);
    }

    @Override
    protected boolean onTransact(int code, Parcel data, Parcel reply, int flags) throws RemoteException {
        if (code == IBinder.INTERFACE_TRANSACTION) {
            if (reply == null) return false;
            reply.writeString(DESCRIPTOR);
            return true;
        }
        if (code != getWifiEnabledStateTransaction && code != setWifiEnabledTransaction
                && code != getCurrentNetworkTransaction)
            return super.onTransact(code, data, reply, flags);
        if (reply == null || data == null) return false;
        data.enforceInterface(DESCRIPTOR);
        if (code == getCurrentNetworkTransaction) {
            data.enforceNoDataAvail();
            int pid = Binder.getCallingPid();
            int uid = Binder.getCallingUid();
            if (context.checkPermission(NETWORK_SETTINGS, pid, uid) != PackageManager.PERMISSION_GRANTED
                    && context.checkPermission(NETWORK_SETUP_WIZARD, pid, uid) != PackageManager.PERMISSION_GRANTED) {
                throw new SecurityException("getCurrentNetwork requires network settings or setup wizard permission");
            }
            synchronized (state) {
                Network current = state.isConnected() && networkSource != null ? networkSource.get() : null;
                if (state.isConnected() && current == null)
                    throw new IllegalStateException("connected Wi-Fi has no registered network");
                reply.writeNoException();
                reply.writeTypedObject(current, 1);
            }
            return true;
        }
        if (code == setWifiEnabledTransaction) {
            String packageName = data.readString();
            data.readBoolean();
            data.enforceNoDataAvail();
            AppOpsManager appOps = (AppOpsManager) context.getSystemService(Context.APP_OPS_SERVICE);
            if (appOps == null) throw new IllegalStateException("guest app-ops unavailable");
            int uid = Binder.getCallingUid();
            int pid = Binder.getCallingPid();
            appOps.checkPackage(uid, packageName);
            if (context.checkPermission(NETWORK_SETTINGS, pid, uid) != PackageManager.PERMISSION_GRANTED) {
                context.enforceCallingPermission(CHANGE_WIFI_STATE, "setWifiEnabled");
                appOps.noteOp(OP_CHANGE_WIFI_STATE, uid, packageName);
            }
            // Remaining policy, lifecycle, callbacks and network transitions are
            // not integrated. Refuse without mutating or pretending success.
            reply.writeNoException();
            reply.writeInt(0);
            return true;
        }
        data.enforceNoDataAvail();
        context.enforceCallingPermission(ACCESS_WIFI_STATE, "getWifiEnabledState");
        reply.writeNoException();
        reply.writeInt(state.isEnabled() ? 3 : 1);
        return true;
    }
}
