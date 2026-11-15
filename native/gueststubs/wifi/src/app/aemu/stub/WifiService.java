/* Modified for AEmulator Super Plus, 2026-10-11: neighbouring networks and signal level.
 * Modified for AEmulator Plus, 2026-11-01: Android 8 - 17 (answers typed by the guest's own interface, listener based connect/save/forget). GPL-3.0; see LICENSE and NOTICE.md. */
/* RedMagicEMU, 2026-11-10: fail closed pending Android 16 permission and lifecycle integration. */
package app.aemu.stub;

import android.content.Context;
import android.content.AttributionSource;
import android.app.AppOpsManager;
import android.content.pm.PackageManager;
import android.location.LocationManager;
import android.os.UserHandle;
import android.os.UserManager;
import android.os.Bundle;
import android.content.Intent;
import android.net.DhcpInfo;
import android.net.Network;
import android.net.wifi.WifiConfiguration;
import android.os.Binder;
import android.os.Handler;
import android.os.IBinder;
import android.os.IInterface;
import android.os.Looper;
import android.os.Message;
import android.os.Messenger;
import android.os.Parcel;
import android.os.Parcelable;
import android.os.SystemClock;

import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.net.InetAddress;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * IWifiManager for a guest that has no wireless hardware: the one network the host configured is always in range, and
 * it is connected while Wi-Fi is switched on and the network has not been forgotten or disconnected. A secured network
 * only accepts its own passphrase.
 *
 * Transaction codes and return types are read from the guest framework's own IWifiManager at start-up, so the same
 * service serves Android 4.0 through 17; methods that are not implemented answer "false / 0 / null" in the type the guest's
 * interface declares (a long, a String, a list, a binder ... - see {@link #writeDefault}).
 */
final class WifiService extends Binder {
    static final String DESCRIPTOR = "android.net.wifi.IWifiManager";
    static final int NET_ID = 0;
    /** Private transaction that only this service answers: how the watcher tells our "wifi" from the framework's own. */
    static final int PROBE = 0x41454d55;
    static final int PROBE_ANSWER = 0x57494649;
    private volatile EarlyWifiProofBinder earlyProof;

    synchronized void setEarlyProof(EarlyWifiProofBinder proof) {
        if (proof == null || earlyProof != null) throw new IllegalStateException("early proof already configured or unavailable");
        earlyProof = proof;
    }

    /** Told when the connection comes or goes (the network agent that carries it into ConnectivityService). */
    interface Listener {
        boolean onConnectionChanged(boolean connected, Object candidateWifiInfo);
    }

    /** Checks ConnectivityService directly; must not wait for work holding the transition lock. */
    interface StartupConfirmation { boolean confirmed(boolean expectedConnected); }

    private static final int STATE_DISABLED = 1;
    private static final int STATE_ENABLED = 3;
    private static final int AP_STATE_DISABLED = 11;

    private final WifiConfig cfg;
    private final Context context;
    private final WifiPersistentState stateStore;
    private final Map<Integer, String> names = new HashMap<Integer, String>();
    private final Map<String, Method> methods = new HashMap<String, Method>();
    private final Map<Integer, Boolean> addedOk = new HashMap<Integer, Boolean>();
    private int nextId = 100;

    private boolean enabled = true;
    private boolean saved = true;
    private boolean down;          // disconnect() was called: stays off until reconnect / enableNetwork / connect
    private boolean connected = true;
    private boolean preparing;
    private boolean activationInProgress, activationInterrupted;
    private boolean networkUnknown;
    private final Object transitionLock = new Object();
    private Listener listener;
    private volatile LegacyWifi legacy;
    private final Messenger messenger;
    private final Handler scanHandler;
    private final List<Object> scanCallbacks = new ArrayList<Object>();
    private final List<IBinder> scanCallbackBinders = new ArrayList<IBinder>();
    private final Map<IBinder, IBinder.DeathRecipient> scanCallbackDeaths = new HashMap<IBinder, IBinder.DeathRecipient>();
    private final java.util.Set<String> unhandled = new java.util.HashSet<String>();

    WifiService(WifiConfig cfg, Looper looper, Context context) throws Exception {
        this(cfg, looper, context, null);
    }

    WifiService(WifiConfig cfg, Looper looper, Context context, WifiPersistentState stateStore) throws Exception {
        this(cfg, looper, context, stateStore, false);
    }

    /** Preparing mode reports no connection until the network handoff has actually completed. */
    WifiService(WifiConfig cfg, Looper looper, Context context, WifiPersistentState stateStore,
            boolean preparing) throws Exception {
        if (context == null) throw new IllegalArgumentException("guest context required");
        this.cfg = cfg;
        this.context = context;
        this.stateStore = stateStore;
        this.preparing = preparing;
        if (stateStore != null) {
            WifiPersistentState.Snapshot state = stateStore.read();
            enabled = state.enabled;
            saved = state.saved;
        }
        connected = !preparing && enabled && saved;
        Class<?> stub = Class.forName(DESCRIPTOR + "$Stub");
        for (Field f : stub.getDeclaredFields()) {
            if (!f.getName().startsWith("TRANSACTION_")) continue;
            f.setAccessible(true);
            names.put(f.getInt(null), f.getName().substring("TRANSACTION_".length()));
        }
        for (Method m : Class.forName(DESCRIPTOR).getMethods()) {
            if (!methods.containsKey(m.getName())) methods.put(m.getName(), m);
        }
        messenger = new Messenger(new ChannelHandler(looper));
        scanHandler = new Handler(looper);
        StringBuilder all = new StringBuilder();
        for (Map.Entry<Integer, String> e : names.entrySet()) all.append(e.getKey()).append('=').append(e.getValue()).append(' ');
        Refl.log("transactions: " + all.toString().trim());
        Refl.log("IWifiManager: " + names.size() + " transactions, " + (connected ? "connected to " : "ready for ") + cfg.ssid
                + (cfg.secured ? " (WPA2)" : " (open)"));
    }

    void setLegacy(LegacyWifi l) {
        legacy = l;
    }

    synchronized boolean isEnabled() {
        return enabled;
    }

    synchronized void setListener(Listener l) {
        listener = l;
    }

    synchronized boolean isConnected() {
        if (networkUnknown) throw new IllegalStateException("Wi-Fi network outcome unknown");
        return connected;
    }

    Object networkTransitionLock() { return transitionLock; }

    void onNetworkUnwanted() {
        final boolean previousEnabled, previousConnected;
        synchronized (transitionLock) {
            synchronized (this) {
                previousEnabled = enabled;
                previousConnected = connected;
                if (activationInProgress) activationInterrupted = true;
                // A successful removal does not resolve an earlier, unrelated failure.
                connected = false; down = true;
            }
            queueStateBroadcast(previousEnabled, previousEnabled, previousConnected, false);
        }
    }

    synchronized void onNetworkFailure(Exception failure) {
        networkUnknown = true;
    }

    private void update() {
        LegacyWifi lg = legacy;
        if (lg != null) lg.changed();
        if (android.os.Build.VERSION.SDK_INT >= 21 && scanHandler != null) {
            scanHandler.removeCallbacks(publishTask);
            scanHandler.postDelayed(publishTask, 600);
        }
        boolean now = enabled && saved && !down;
        Listener l = null;
        synchronized (this) {
            if (now == connected) return;
            connected = now;
            l = listener;
        }
        if (l == null || !l.onConnectionChanged(now, now ? wifiInfo(true) : null)) {
            synchronized (this) { connected = !now; }
            throw new IllegalStateException("Wi-Fi network transition failed");
        }
        Refl.log(now ? "connected to " + cfg.ssid : "disconnected");
    }

    // ------------------------------------------------------------------ credentials

    private static String unquote(String s) {
        if (s == null) return null;
        if (s.length() >= 2 && s.startsWith("\"") && s.endsWith("\"")) return s.substring(1, s.length() - 1);
        return s;
    }

    /** True when [c] names our network and carries its passphrase (or none, for an open network). */
    private boolean credentialsOk(WifiConfiguration c) {
        if (c == null || !WifiConfig.matchesSsid(cfg.ssid, c.SSID)) return false;
        String psk = unquote(c.preSharedKey);
        if (psk != null && !psk.isEmpty()) return false;
        if (c.allowedKeyManagement == null || c.allowedKeyManagement.nextSetBit(1) >= 0) return false;
        if (c.wepKeys != null) for (String key : c.wepKeys) if (key != null && !key.isEmpty()) return false;
        return true;
    }

    private static WifiConfiguration readConfig(Parcel data) {
        if (data.dataAvail() < 4 || data.readInt() == 0) return null;
        // CREATOR is hidden in the public SDK; the field is there at run time
        try {
            @SuppressWarnings("unchecked") android.os.Parcelable.Creator<WifiConfiguration> c =
                (android.os.Parcelable.Creator<WifiConfiguration>) WifiConfiguration.class.getField("CREATOR").get(null);
            return c.createFromParcel(data);
        } catch (Exception e) {
            throw new IllegalStateException(e);
        }
    }

    // ------------------------------------------------------------------ objects handed to apps

    private Object wifiSsid() {
        return wifiSsid(cfg.ssid);
    }

    private static Object wifiSsid(String name) {
        Class<?> c = Refl.cls("android.net.wifi.WifiSsid");
        byte[] raw;
        try {
            raw = name.getBytes("UTF-8");
        } catch (Throwable t) {
            raw = name.getBytes();
        }
        try {
            Object s = Refl.callStatic(c, "createFromHex", Refl.hex(raw));
            if (s != null) return s;
        } catch (Throwable ignored) { }
        Object s = Refl.callStatic(c, "createFromAsciiEncoded", name);
        // 12+: the factories are named after what they take
        if (s == null) s = Refl.callStatic(c, "fromBytes", raw);
        if (s == null) s = Refl.callStatic(c, "createFromByteArray", raw);
        if (s == null) s = Refl.callStatic(c, "fromUtf8Text", name);
        return s;
    }

    Object wifiInfo() {
        synchronized (transitionLock) {
            return wifiInfo(isConnected());
        }
    }

    Object wifiInfo(boolean associated) {
        Object wi = Refl.newNoArg("android.net.wifi.WifiInfo");
        if (wi == null || !associated) return wi;
        // This single virtual AP is the primary Wi-Fi connection when associated.
        // Internet validation remains the responsibility of NetworkMonitor.
        try {
            wi.getClass().getMethod("setIsPrimary", boolean.class).invoke(wi, true);
        } catch (ReflectiveOperationException failure) {
            throw new IllegalStateException("cannot mark virtual Wi-Fi as primary", failure);
        }
        // before 4.2 the name is a plain String, from 4.2 a WifiSsid
        Object ssid = wifiSsid();
        Refl.call(wi, "setSSID", ssid != null ? ssid : cfg.ssid);
        Refl.call(wi, "setBSSID", cfg.bssid);
        Refl.call(wi, "setMacAddress", cfg.mac);
        Refl.call(wi, "setNetworkId", Integer.valueOf(NET_ID));
        Refl.call(wi, "setRssi", Integer.valueOf(cfg.rssi));
        Refl.call(wi, "setLinkSpeed", Integer.valueOf(cfg.speed));
        Refl.call(wi, "setFrequency", Integer.valueOf(cfg.frequency));
        Refl.call(wi, "setMeteredHint", Boolean.FALSE);
        try {
            Refl.call(wi, "setInetAddress", InetAddress.getByName(cfg.ip));
        } catch (Throwable ignored) { }
        try {
            Class<?> ss = Class.forName("android.net.wifi.SupplicantState");
            Object completed = ss.getField("COMPLETED").get(null);
            Refl.call(wi, "setSupplicantState", completed);
        } catch (Throwable ignored) { }
        return wi;
    }

    private Object scanResult() {
        return scanResult(cfg.ssid, cfg.bssid, cfg.secured ? "[WPA2-PSK-CCMP][ESS]" : "[ESS]", cfg.rssi, cfg.frequency);
    }

    /** One entry of the scan list: our network, or one of the visible-but-locked neighbours (WifiConfig.neighbors). */
    private static Object scanResult(String name, String bssid, String caps, int rssi, int frequency) {
        Class<?> ssidClass = Refl.cls("android.net.wifi.WifiSsid");
        Object ssid = wifiSsid(name);
        Object r = null;
        if (ssidClass != null) {
            r = Refl.create("android.net.wifi.ScanResult",
                    new Class<?>[] { ssidClass, String.class, String.class, int.class, int.class, long.class },
                    ssid, bssid, caps, Integer.valueOf(rssi),
                    Integer.valueOf(frequency), Long.valueOf(SystemClock.elapsedRealtime() * 1000L));
        } else {
            // before 4.2: ScanResult(String ssid, String bssid, String caps, int level, int frequency)
            r = Refl.create("android.net.wifi.ScanResult",
                    new Class<?>[] { String.class, String.class, String.class, int.class, int.class },
                    name, bssid, caps, Integer.valueOf(rssi), Integer.valueOf(frequency));
        }
        if (r == null && ssidClass != null) {
            // newer frameworks: a longer constructor that starts with the same six parameters
            Class<?> sr = Refl.cls("android.net.wifi.ScanResult");
            if (sr != null) {
                for (java.lang.reflect.Constructor<?> k : sr.getDeclaredConstructors()) {
                    Class<?>[] p = k.getParameterTypes();
                    if (p.length < 6 || p[0] != ssidClass || p[1] != String.class || p[2] != String.class) continue;
                    Object[] a = new Object[p.length];
                    for (int i = 0; i < p.length; i++) a[i] = Refl.defaultFor(p[i]);
                    a[0] = ssid; a[1] = bssid; a[2] = caps;
                    a[3] = Integer.valueOf(rssi); a[4] = Integer.valueOf(frequency);
                    a[5] = Long.valueOf(SystemClock.elapsedRealtime() * 1000L);
                    try {
                        k.setAccessible(true);
                        r = k.newInstance(a);
                        break;
                    } catch (Throwable t) {
                        Refl.log("ScanResult constructor: " + t);
                    }
                }
            }
        }
        if (r == null) {
            // Android 12+: the (WifiSsid, String, String, int, int, long) constructor is gone and the long one starts
            // (WifiSsid, String, long, ...). The plain constructor still exists; the public fields are filled in below.
            r = Refl.newNoArg("android.net.wifi.ScanResult");
        }
        if (r == null) return null;
        Refl.setField(r, "SSID", name);
        Refl.setField(r, "BSSID", bssid);
        Refl.setField(r, "capabilities", caps);
        Refl.setField(r, "level", Integer.valueOf(rssi));
        Refl.setField(r, "frequency", Integer.valueOf(frequency));
        Refl.setField(r, "timestamp", Long.valueOf(SystemClock.elapsedRealtime() * 1000L));
        if (ssid != null) Refl.setField(r, "wifiSsid", ssid);
        return r;
    }

    private WifiConfiguration savedConfig() {
        WifiConfiguration c = new WifiConfiguration();
        c.SSID = "\"" + cfg.ssid + "\"";
        c.networkId = NET_ID;
        c.status = isConnected() ? WifiConfiguration.Status.CURRENT : WifiConfiguration.Status.ENABLED;
        if (cfg.secured) {
            c.allowedKeyManagement.set(WifiConfiguration.KeyMgmt.WPA_PSK);
            c.allowedProtocols.set(WifiConfiguration.Protocol.RSN);
            c.allowedPairwiseCiphers.set(WifiConfiguration.PairwiseCipher.CCMP);
            c.allowedGroupCiphers.set(WifiConfiguration.GroupCipher.CCMP);
        } else {
            c.allowedKeyManagement.set(WifiConfiguration.KeyMgmt.NONE);
        }
        return c;
    }

    private DhcpInfo dhcpInfo() {
        DhcpInfo d = new DhcpInfo();
        d.ipAddress = WifiConfig.ipInt(cfg.ip);
        d.gateway = WifiConfig.ipInt(cfg.gateway);
        d.netmask = WifiConfig.netmaskInt(cfg.prefix);
        d.dns1 = WifiConfig.ipInt(cfg.dns1);
        d.dns2 = WifiConfig.ipInt(cfg.dns2);
        d.serverAddress = WifiConfig.ipInt(cfg.gateway);
        d.leaseDuration = 86400;
        return d;
    }

    // ------------------------------------------------------------------ transport

    /** A list result: 4.4 returns List<T>, newer frameworks ParceledListSlice<T>, whatever the guest's interface says. */
    private void writeList(String method, List<? extends Parcelable> list, Parcel reply) {
        Method m = methods.get(method);
        boolean slice = m != null && m.getReturnType().getName().endsWith("ParceledListSlice");
        if (!slice) {
            reply.writeTypedList(list);
            return;
        }
        Object s = Refl.create(m.getReturnType().getName(), new Class<?>[] { List.class }, list);
        if (s instanceof Parcelable) {
            reply.writeInt(1);
            ((Parcelable) s).writeToParcel(reply, Parcelable.PARCELABLE_WRITE_RETURN_VALUE);
        } else {
            reply.writeInt(0);
        }
    }

    /** An empty answer in the type the guest's interface declares: a slice or list with no items, an empty map. */
    private void writeEmpty(String name, Parcel reply) {
        Method m = methods.get(name);
        Class<?> t = m == null ? List.class : m.getReturnType();
        if (t.getName().endsWith("ParceledListSlice")) writeList(name, new ArrayList<Parcelable>(), reply);
        else if (Map.class.isAssignableFrom(t)) reply.writeInt(0);
        else reply.writeTypedList(new ArrayList<Parcelable>());
    }

    private void writeDefaultBool(String name, Parcel reply) {
        Method m = methods.get(name);
        if (m == null || m.getReturnType() != void.class) reply.writeInt(0);
    }

    private static void writeParcelable(Parcel reply, Parcelable p) {
        if (p == null) {
            reply.writeInt(0);
        } else {
            reply.writeInt(1);
            p.writeToParcel(reply, Parcelable.PARCELABLE_WRITE_RETURN_VALUE);
        }
    }

    @Override
    protected boolean onTransact(int code, Parcel data, Parcel reply, int flags) throws android.os.RemoteException {
        if (code == INTERFACE_TRANSACTION) {
            if (reply != null) reply.writeString(DESCRIPTOR);
            return true;
        }
        if (code == EarlyWifiProofBinder.PROOF && earlyProof != null) return earlyProof.answer(data, reply);
        if (code == PROBE) {
            if (reply != null) reply.writeInt(PROBE_ANSWER);
            return true;
        }
        String name = names.get(Integer.valueOf(code));
        if (name == null) return super.onTransact(code, data, reply, flags);
        // Only methods whose Android 16 permissions have been audited may be
        // served. Unsupported transactions must not receive success replies.
        if ((!WifiServiceGuard.isStateQuery(name) && !WifiServiceGuard.isSignalLevel(name)
                && !WifiServiceGuard.isVerboseLoggingQuery(name)
                && !WifiServiceGuard.isScanCallback(name)
                && !WifiServiceGuard.isPrivilegedScan(name) && !WifiServiceGuard.isConnectionInfo(name)
                && !WifiServiceGuard.isPrivilegedMutation(name) && !WifiServiceGuard.isConfiguredQuery(name)
                && !WifiServiceGuard.isPrivilegedConfiguredQuery(name)
                && !WifiServiceGuard.isCurrentNetwork(name) && !WifiServiceGuard.isSuggestionMatch(name)
                && !WifiServiceGuard.isPasspointQuery(name))
                || (reply == null && !(("connect".equals(name) || "forget".equals(name))
                        && (flags & IBinder.FLAG_ONEWAY) != 0))) return false;
        data.enforceInterface(DESCRIPTOR);
        if (WifiServiceGuard.isVerboseLoggingQuery(name)) {
            data.enforceNoDataAvail();
            // This virtual service does not enable verbose Wi-Fi logging.
            reply.writeNoException();
            reply.writeInt(0);
            return true;
        }
        if (WifiServiceGuard.isSignalLevel(name)) {
            if (data.dataAvail() < 4) throw new IllegalArgumentException("signal level requires RSSI");
            int rssi = data.readInt();
            data.enforceNoDataAvail();
            // The NX809J target ROM's original service uses these RSSI boundaries.
            int[] thresholds = { -90, -85, -75, -65 };
            int level = 0;
            while (level < thresholds.length && rssi >= thresholds[level]) level++;
            reply.writeNoException();
            reply.writeInt(level);
            return true;
        }
        if (WifiServiceGuard.isPasspointQuery(name)) {
            String packageName = data.readString();
            data.enforceNoDataAvail();
            checkPackage(packageName);
            Parcelable result = parcelableSlice(name, new ArrayList<Parcelable>());
            reply.writeNoException();
            writeParcelable(reply, result);
            return true;
        }
        if (WifiServiceGuard.isSuggestionMatch(name)) {
            int pid = Binder.getCallingPid(), uid = Binder.getCallingUid();
            if (!hasPermission("android.permission.NETWORK_SETTINGS", pid, uid)
                    && !hasPermission("android.permission.NETWORK_SETUP_WIZARD", pid, uid))
                throw new SecurityException("suggestion matching requires settings permission");
            try {
                Method method = methods.get(name);
                if (method == null || method.getParameterTypes().length != 1)
                    throw new IllegalStateException("suggestion matching interface unavailable");
                Class<?> type = method.getParameterTypes()[0];
                Parcelable.Creator<?> creator = (Parcelable.Creator<?>) type.getField("CREATOR").get(null);
                data.readTypedObject(creator);
                data.enforceNoDataAvail();
                Parcelable result = parcelableSlice(name, new ArrayList<Parcelable>());
                reply.writeNoException();
                writeParcelable(reply, result);
                return true;
            } catch (ReflectiveOperationException failure) {
                throw new IllegalStateException("suggestion matching input unavailable", failure);
            }
        }
        if (WifiServiceGuard.isCurrentNetwork(name)) {
            data.enforceNoDataAvail();
            int pid = Binder.getCallingPid(), uid = Binder.getCallingUid();
            if (!hasPermission("android.permission.NETWORK_SETTINGS", pid, uid)
                    && !hasPermission("android.permission.NETWORK_SETUP_WIZARD", pid, uid))
                throw new SecurityException("getCurrentNetwork requires settings permission");
            Network active;
            synchronized (transitionLock) {
                boolean associated = isConnected();
                active = WifiStub.network();
                if (associated != (active != null)) throw new IllegalStateException("Wi-Fi network state unconfirmed");
            }
            reply.writeNoException();
            writeParcelable(reply, active);
            return true;
        }
        if (WifiServiceGuard.isPrivilegedConfiguredQuery(name)) {
            String packageName = data.readString();
            data.readString(); // attribution tag
            Bundle extras = data.readTypedObject(Bundle.CREATOR);
            data.enforceNoDataAvail();
            context.enforceCallingPermission("android.permission.READ_WIFI_CREDENTIAL", name);
            context.enforceCallingPermission("android.permission.ACCESS_WIFI_STATE", name);
            checkPackage(packageName);
            int uid = Binder.getCallingUid(), pid = Binder.getCallingPid();
            if (!hasPermission("android.permission.NETWORK_SETTINGS", pid, uid)
                    && !hasPermission("android.permission.NETWORK_SETUP_WIZARD", pid, uid))
                throw new SecurityException("privileged configuration requires settings permission");
            AttributionSource source = extras == null ? null : extras.getParcelable(
                    "EXTRA_PARAM_KEY_ATTRIBUTION_SOURCE", AttributionSource.class);
            // Only direct Settings/SUW attribution is audited; chained delegation and
            // other privileged callers remain unavailable until fully implemented.
            if (source == null || !source.checkCallingUid() || source.getUid() != uid
                    || !packageName.equals(source.getPackageName()) || source.getNext() != null)
                throw new SecurityException("invalid privileged configuration attribution");
            List<Parcelable> configs = new ArrayList<Parcelable>();
            synchronized (this) { if (saved) configs.add(savedConfig()); }
            Parcelable slice = parcelableSlice(name, configs);
            reply.writeNoException();
            writeParcelable(reply, slice);
            return true;
        }
        if (WifiServiceGuard.isConfiguredQuery(name)) {
            boolean secondary = "getConfiguredNetworks2".equals(name);
            if (secondary) data.readInt();
            String packageName = data.readString();
            data.readString(); // attribution tag
            if (!secondary) data.readBoolean(); // callerNetworksOnly
            data.enforceNoDataAvail();
            context.enforceCallingPermission("android.permission.ACCESS_WIFI_STATE", name);
            context.enforceCallingPermission("android.permission.NETWORK_SETTINGS", name);
            checkPackage(packageName);
            List<Parcelable> configs = new ArrayList<Parcelable>();
            synchronized (this) { if (!secondary && saved) configs.add(savedConfig()); }
            Parcelable slice = parcelableSlice(name, configs);
            reply.writeNoException();
            writeParcelable(reply, slice);
            return true;
        }
        if (WifiServiceGuard.isPrivilegedMutation(name)) {
            handlePrivilegedMutation(name, data, reply);
            return true;
        }
        if (WifiServiceGuard.isConnectionInfo(name)) {
            String packageName = data.readString();
            String attributionTag = data.readString();
            data.enforceNoDataAvail();
            context.enforceCallingPermission("android.permission.ACCESS_WIFI_STATE", name);
            AppOpsManager appOps = (AppOpsManager) context.getSystemService(Context.APP_OPS_SERVICE);
            if (appOps == null) throw new IllegalStateException("guest app-ops unavailable");
            int uid = Binder.getCallingUid();
            int pid = Binder.getCallingPid();
            appOps.checkPackage(uid, packageName);
            boolean settings = hasPermission("android.permission.NETWORK_SETTINGS", pid, uid)
                    || hasPermission("android.permission.NETWORK_SETUP_WIZARD", pid, uid);
            boolean localMac = hasPermission("android.permission.LOCAL_MAC_ADDRESS", pid, uid);
            boolean location = settings;
            if (!location && UserHandle.getUserHandleForUid(uid).equals(android.os.Process.myUserHandle())
                    && hasPermission("android.permission.ACCESS_FINE_LOCATION", pid, uid)) {
                long identity = Binder.clearCallingIdentity();
                try {
                    LocationManager manager = (LocationManager) context.getSystemService(Context.LOCATION_SERVICE);
                    if (manager != null && manager.isLocationEnabled()) {
                        location = appOps.noteOpNoThrow("android:fine_location", uid, packageName, attributionTag, null)
                                        == AppOpsManager.MODE_ALLOWED
                                && appOps.noteOpNoThrow("android:wifi_scan", uid, packageName, attributionTag, null)
                                        == AppOpsManager.MODE_ALLOWED;
                    }
                } finally { Binder.restoreCallingIdentity(identity); }
            }
            Object info = WifiPrivacy.redactedFor(wifiInfo(), location, localMac, settings);
            if (!(info instanceof Parcelable)) throw new IllegalStateException("Wi-Fi info not parcelable");
            reply.writeNoException();
            writeParcelable(reply, (Parcelable) info);
            return true;
        }
        if (WifiServiceGuard.isPrivilegedScan(name)) {
            String packageName = data.readString();
            String attributionTag = data.readString();
            data.enforceNoDataAvail();
            boolean start = "startScan".equals(name);
            if (!start) context.enforceCallingPermission("android.permission.ACCESS_WIFI_STATE", name);
            boolean allowed = WifiScanAccess.allowed(context, Binder.getCallingUid(), Binder.getCallingPid(),
                    packageName, attributionTag, start);
            if (!allowed) {
                reply.writeNoException();
                reply.writeInt(0); // false start or null scan list
                return true;
            }
            if (start) {
                final boolean started = isEnabled() && scanHandler.post(new Runnable() {
                    @Override public void run() {
                        Intent broadcast = new Intent("android.net.wifi.SCAN_RESULTS");
                        broadcast.putExtra("resultsUpdated", true);
                        context.sendBroadcast(broadcast);
                        List<Object> callbacks;
                        synchronized (scanCallbacks) { callbacks = new ArrayList<Object>(scanCallbacks); }
                        for (Object callback : callbacks) Refl.call(callback, "onScanResultsAvailable");
                    }
                });
                reply.writeNoException();
                reply.writeInt(started ? 1 : 0);
                return true;
            }
            List<Parcelable> results = new ArrayList<Parcelable>();
            if (isEnabled()) results.add(virtualScanResult());
            Parcelable slice = parcelableSlice("getScanResults", results);
            reply.writeNoException();
            writeParcelable(reply, slice);
            return true;
        }
        if (WifiServiceGuard.isScanCallback(name)) {
            IBinder callback = data.readStrongBinder();
            data.enforceNoDataAvail();
            if (callback == null) throw new IllegalArgumentException("scan callback required");
            context.enforceCallingPermission("android.permission.ACCESS_WIFI_STATE", name);
            if ("registerScanResultsCallback".equals(name)) registerScanCallback(callback);
            else unregisterScanCallback(callback);
            reply.writeNoException();
            return true;
        }
        data.enforceNoDataAvail();
        context.enforceCallingPermission("android.permission.ACCESS_WIFI_STATE", name);
        handle(name, data, reply);
        return true;
    }

    private boolean hasPermission(String permission, int pid, int uid) {
        return context.checkPermission(permission, pid, uid) == PackageManager.PERMISSION_GRANTED;
    }

    /** Apply the Guest airplane-mode transition through the normal network state path. */
    boolean disableForAirplaneMode() {
        synchronized (transitionLock) {
            final boolean currentSaved, currentDown;
            synchronized (this) {
                if (!enabled) return !networkUnknown;
                currentSaved = saved;
                currentDown = down;
            }
            return transition(false, currentSaved, currentDown);
        }
    }

    boolean restoreAfterAirplaneMode() {
        synchronized (transitionLock) {
            final boolean currentSaved, currentDown;
            synchronized (this) {
                if (enabled) return !networkUnknown;
                currentSaved = saved;
                currentDown = down;
            }
            return transition(true, currentSaved, currentDown);
        }
    }

    /** The standalone process reads Guest settings through the external provider API. */
    void startAirplaneModeObserver() {
        final WifiAirplaneModeObserver observer = new WifiAirplaneModeObserver(this::isEnabled,
                value -> value ? restoreAfterAirplaneMode() : disableForAirplaneMode());
        Thread watcher = new Thread(() -> {
            boolean failed = false;
            while (!Thread.currentThread().isInterrupted()) {
                try {
                    int mode = WifiSettingsReader.airplaneMode(context);
                    if (mode != 0 && mode != 1) throw new IllegalStateException("invalid airplane mode");
                    observer.observe(mode == 1);
                    failed = false;
                } catch (RuntimeException failure) {
                    if (!failed) Refl.log("airplane mode observer unavailable: " + failure.getClass().getName());
                    failed = true;
                }
                try { Thread.sleep(1000); }
                catch (InterruptedException interrupted) { Thread.currentThread().interrupt(); }
            }
        }, "redmagic-wifi-airplane");
        watcher.setDaemon(true);
        watcher.start();
    }

    /** Commits state only after the network transition has succeeded. */
    private boolean transition(boolean nextEnabled, boolean nextSaved, boolean nextDown) {
        synchronized (transitionLock) {
            boolean oldConnected, oldEnabled, oldSaved;
            Listener current;
            synchronized (this) {
                if (networkUnknown || activationInProgress) return false;
                if (preparing && nextEnabled && nextSaved && !nextDown) return false;
                oldConnected = connected;
                oldEnabled = enabled;
                oldSaved = saved;
                current = listener;
            }
            boolean nextConnected = nextEnabled && nextSaved && !nextDown;
            if (nextConnected && current == null) return false;
            if (oldConnected != nextConnected) {
                Object info = nextConnected ? wifiInfo(true) : null;
                if (current == null || (nextConnected && info == null)
                        || !current.onConnectionChanged(nextConnected, info)) return false;
            }
            synchronized (this) { if (networkUnknown) return false; }
            if (stateStore != null && (oldEnabled != nextEnabled || oldSaved != nextSaved)) {
                try {
                    stateStore.write(nextEnabled, nextSaved);
                } catch (java.io.IOException | RuntimeException failure) {
                    Refl.log("Wi-Fi state save failed: " + failure);
                    if (oldConnected != nextConnected) {
                        try {
                            Object oldInfo = oldConnected ? wifiInfo(true) : null;
                            if ((oldConnected && oldInfo == null)
                                    || !current.onConnectionChanged(oldConnected, oldInfo)) {
                                synchronized (this) { networkUnknown = true; }
                            }
                        } catch (RuntimeException rollbackFailure) {
                            synchronized (this) { networkUnknown = true; }
                            Refl.log("Wi-Fi state rollback failed: " + rollbackFailure);
                        }
                    }
                    boolean diskRestored = false;
                    try {
                        WifiPersistentState.Snapshot disk = stateStore.read();
                        if (disk.enabled != oldEnabled || disk.saved != oldSaved) {
                            try { stateStore.write(oldEnabled, oldSaved); }
                            catch (java.io.IOException | RuntimeException restoreFailure) {
                                Refl.log("Wi-Fi persisted state restore failed: " + restoreFailure);
                            }
                            disk = stateStore.read();
                        }
                        diskRestored = disk.enabled == oldEnabled && disk.saved == oldSaved;
                    } catch (java.io.IOException | RuntimeException readFailure) {
                        Refl.log("Wi-Fi persisted state verification failed: " + readFailure);
                    }
                    if (!diskRestored || failure instanceof WifiPersistentState.DurabilityException)
                        synchronized (this) { networkUnknown = true; }
                    return false;
                }
            }
            synchronized (this) {
                if (networkUnknown) {
                    if (stateStore != null && (oldEnabled != nextEnabled || oldSaved != nextSaved)) {
                        try { stateStore.write(oldEnabled, oldSaved); }
                        catch (java.io.IOException | RuntimeException failure) { Refl.log("Wi-Fi state rollback failed: " + failure); }
                    }
                    return false;
                }
                enabled = nextEnabled;
                saved = nextSaved;
                down = nextDown;
                connected = nextConnected;
            }
            queueStateBroadcast(oldEnabled, nextEnabled, oldConnected, nextConnected);
            queueConfiguredBroadcast(oldSaved, nextSaved);
            return true;
        }
    }

    private boolean onServiceLooper() { return Looper.myLooper() == scanHandler.getLooper(); }

    /**
     * Finish preparation only after the caller has made network handoff possible.
     * The caller must run off the service Looper after that Looper has started.
     * Do not hold transitionLock while awaiting network callbacks on that Looper.
     */
    boolean finishPreparation(StartupConfirmation confirmation) {
        if (confirmation == null) throw new IllegalArgumentException("network confirmation required");
        if (onServiceLooper()) throw new IllegalStateException("cannot wait for network on service Looper");
        final boolean wantedEnabled, wantedConnected;
        final Listener current;
        synchronized (transitionLock) {
            synchronized (this) {
                if (!preparing || networkUnknown || activationInProgress) return false;
                wantedEnabled = enabled;
                wantedConnected = enabled && saved && !down;
                current = listener;
                if (wantedConnected && current == null) return false;
                activationInProgress = true;
                activationInterrupted = false;
            }
        }
        try {
            if (wantedConnected) {
                Object info = wifiInfo(true);
                if (info == null) return false;
                boolean requested = current.onConnectionChanged(true, info);
                synchronized (this) { if (networkUnknown) return false; }
                if (!requested) {
                    if (!confirmation.confirmed(false)) synchronized (this) { networkUnknown = true; }
                    return false;
                }
            }
            boolean confirmed = confirmation.confirmed(wantedConnected);
            boolean interrupted;
            synchronized (transitionLock) {
                synchronized (this) {
                    interrupted = networkUnknown || activationInterrupted || enabled != wantedEnabled
                            || (enabled && saved && !down) != wantedConnected;
                    if (confirmed && !interrupted) {
                        preparing = false;
                        connected = wantedConnected;
                    }
                }
                if (confirmed && !interrupted)
                    queueStateBroadcast(wantedEnabled, wantedEnabled, false, wantedConnected);
            }
            if (confirmed && !interrupted) return true;
            boolean removed = wantedConnected && current.onConnectionChanged(false, null);
            if (!removed || !confirmation.confirmed(false)) synchronized (this) { networkUnknown = true; }
            return false;
        } catch (RuntimeException | Error failure) {
            // A callback or registration query may have changed state before failing.
            synchronized (this) { preparing = true; networkUnknown = true; }
            throw failure;
        } finally {
            synchronized (transitionLock) { synchronized (this) { activationInProgress = false; } }
        }
    }

    /** Snapshot and enqueue under the transition lock; actual delivery never holds it. */
    void queueInitialBroadcast() {
        synchronized (transitionLock) {
            final boolean currentEnabled, currentConnected;
            synchronized (this) {
                if (networkUnknown) {
                    Refl.log("initial_broadcast_not_sent_network_unknown");
                    return;
                }
                currentEnabled = enabled;
                currentConnected = connected;
            }
            if (!scanHandler.post(new Runnable() {
                @Override public void run() {
                    try {
                        WifiStateBroadcasts.publishInitial(context, currentEnabled, currentConnected);
                        Refl.log("initial_broadcast_sent");
                    } catch (WifiStateBroadcasts.InitialBroadcastFailure failure) {
                        Throwable cause = failure.getCause();
                        Refl.log("initial_broadcast_failed_" + failure.stage + "_"
                                + (cause == null ? failure.getClass().getName() : cause.getClass().getName()));
                    } catch (RuntimeException failure) {
                        Refl.log("initial_broadcast_failed_unknown_" + failure.getClass().getName());
                    }
                }
            })) Refl.log("initial_broadcast_enqueue_rejected");
        }
    }

    private void queueConfiguredBroadcast(boolean oldSaved, final boolean newSaved) {
        if (oldSaved == newSaved) return;
        if (!scanHandler.post(new Runnable() {
            @Override public void run() {
                try { WifiStateBroadcasts.publishConfigured(context, newSaved); }
                catch (RuntimeException failure) { Refl.log("Wi-Fi configuration broadcast failed: " + failure); }
            }
        })) Refl.log("Wi-Fi configuration broadcast rejected: looper stopped");
    }

    private void queueStateBroadcast(final boolean oldEnabled, final boolean newEnabled,
            final boolean oldConnected, final boolean newConnected) {
        if (oldEnabled == newEnabled && oldConnected == newConnected) return;
        if (!scanHandler.post(new Runnable() {
            @Override public void run() {
                try { WifiStateBroadcasts.publish(context, oldEnabled, newEnabled, oldConnected, newConnected); }
                catch (RuntimeException failure) { Refl.log("Wi-Fi state broadcast failed: " + failure); }
            }
        })) Refl.log("Wi-Fi state broadcast rejected: looper stopped");
    }

    private void checkPackage(String packageName) {
        AppOpsManager appOps = (AppOpsManager) context.getSystemService(Context.APP_OPS_SERVICE);
        if (appOps == null) throw new IllegalStateException("guest app-ops unavailable");
        appOps.checkPackage(Binder.getCallingUid(), packageName);
    }

    private boolean togglePermitted() {
        int uid = Binder.getCallingUid();
        if (!UserHandle.getUserHandleForUid(uid).equals(android.os.Process.myUserHandle())) return false;
        long identity = Binder.clearCallingIdentity();
        try {
            UserManager users = (UserManager) context.getSystemService(Context.USER_SERVICE);
            if (users == null) throw new IllegalStateException("guest user service unavailable");
            if (users.hasUserRestriction(UserManager.DISALLOW_CHANGE_WIFI_STATE)) return false;
            return WifiSettingsReader.satelliteMode(context) == 0;
        } finally { Binder.restoreCallingIdentity(identity); }
    }

    private void handlePrivilegedMutation(String name, Parcel data, Parcel reply) {
        context.enforceCallingPermission("android.permission.NETWORK_SETTINGS", name);
        if ("disconnect".equals(name) || "disconnect2".equals(name) || "reconnect".equals(name)
                || "enableNetwork".equals(name)) {
            int id = NET_ID;
            if ("enableNetwork".equals(name)) { id = data.readInt(); data.readBoolean(); }
            String packageName = data.readString();
            data.enforceNoDataAvail();
            checkPackage(packageName);
            boolean success = false;
            if (!"disconnect2".equals(name)) synchronized (transitionLock) {
                boolean oldEnabled, oldSaved;
                synchronized (this) { oldEnabled = enabled; oldSaved = saved; }
                if ("disconnect".equals(name)) success = transition(oldEnabled, oldSaved, true);
                else if (oldEnabled && oldSaved && id == NET_ID) success = transition(true, true, false);
            }
            reply.writeNoException();
            reply.writeInt(success ? 1 : 0);
            return;
        }
        if ("setWifiEnabled".equals(name)) {
            String packageName = data.readString();
            boolean value = data.readBoolean();
            data.enforceNoDataAvail();
            checkPackage(packageName);
            boolean success = false;
            if (togglePermitted()) synchronized (transitionLock) {
                boolean oldSaved, oldDown;
                synchronized (this) { oldSaved = saved; oldDown = down; }
                success = transition(value, oldSaved, oldDown);
            }
            reply.writeNoException();
            reply.writeInt(success ? 1 : 0);
            return;
        }
        if ("forget".equals(name)) {
            int id = data.readInt();
            IBinder callback = data.readStrongBinder();
            data.enforceNoDataAvail();
            boolean success;
            synchronized (transitionLock) {
                boolean oldEnabled, oldSaved;
                synchronized (this) { oldEnabled = enabled; oldSaved = saved; }
                success = id == NET_ID && oldSaved && transition(oldEnabled, false, true);
            }
            actionCallback(callback, success);
            if (reply != null) reply.writeNoException();
            return;
        }
        WifiConfiguration configuration = readConfig(data);
        int id = data.readInt();
        IBinder callback = data.readStrongBinder();
        String packageName = data.readString();
        Bundle extras = data.readTypedObject(Bundle.CREATOR);
        data.enforceNoDataAvail();
        checkPackage(packageName);
        if (Binder.getCallingUid() % 100000 == 1000) {
            AttributionSource source = extras == null ? null : extras.getParcelable("EXTRA_PARAM_KEY_ATTRIBUTION_SOURCE", AttributionSource.class);
            if (source == null || !source.checkCallingUid()) throw new SecurityException("connect attribution unavailable");
            for (AttributionSource item = source; item.getNext() != null; item = item.getNext())
                if (!item.isTrusted(context)) throw new SecurityException("untrusted connect attribution");
        }
        boolean success;
        synchronized (transitionLock) {
            boolean oldEnabled, oldSaved;
            synchronized (this) { oldEnabled = enabled; oldSaved = saved; }
            boolean credentialOk = configuration != null ? credentialsOk(configuration) : id == NET_ID && oldSaved;
            success = oldEnabled && credentialOk && transition(true, true, false);
        }
        actionCallback(callback, success);
        if (reply != null) reply.writeNoException();
    }

    private void actionCallback(IBinder binder, boolean success) {
        if (binder == null) return;
        try {
            Class<?> type = Class.forName("android.net.wifi.IActionListener");
            Object callback = Class.forName("android.net.wifi.IActionListener$Stub")
                    .getMethod("asInterface", IBinder.class).invoke(null, binder);
            if (callback == null) throw new IllegalStateException("Wi-Fi action callback unavailable");
            if (success) type.getMethod("onSuccess").invoke(callback);
            else type.getMethod("onFailure", int.class).invoke(callback, 0);
        } catch (ReflectiveOperationException failure) {
            throw new IllegalStateException("Wi-Fi action callback failed", failure);
        }
    }

    private Parcelable virtualScanResult() {
        android.net.wifi.ScanResult result = new android.net.wifi.ScanResult();
        result.BSSID = cfg.bssid;
        result.capabilities = cfg.secured ? "[WPA2-PSK-CCMP][ESS]" : "[ESS]";
        result.level = cfg.rssi;
        result.frequency = cfg.frequency;
        result.timestamp = SystemClock.elapsedRealtime() * 1000L;
        try {
            android.net.wifi.WifiSsid ssid = android.net.wifi.WifiSsid.fromBytes(
                    cfg.ssid.getBytes(java.nio.charset.StandardCharsets.UTF_8));
            result.getClass().getMethod("setWifiSsid", android.net.wifi.WifiSsid.class).invoke(result, ssid);
        } catch (ReflectiveOperationException failure) {
            throw new IllegalStateException("cannot construct virtual scan SSID", failure);
        }
        return result;
    }

    private Parcelable parcelableSlice(String name, List<Parcelable> results) {
        try {
            Method method = methods.get(name);
            if (method == null) throw new IllegalStateException("list return type unavailable");
            Object slice = method.getReturnType().getConstructor(List.class).newInstance(results);
            if (!(slice instanceof Parcelable)) throw new IllegalStateException("list return type is not parcelable");
            return (Parcelable) slice;
        } catch (ReflectiveOperationException failure) {
            throw new IllegalStateException("cannot construct result list", failure);
        }
    }

    private void registerScanCallback(final IBinder binder) throws android.os.RemoteException {
        synchronized (scanCallbacks) {
            if (scanCallbackBinders.contains(binder)) return;
            final Object callback;
            try {
                Class<?> stub = Class.forName("android.net.wifi.IScanResultsCallback$Stub");
                callback = stub.getMethod("asInterface", IBinder.class).invoke(null, binder);
            } catch (ReflectiveOperationException failure) {
                throw new IllegalStateException("scan callback interface unavailable", failure);
            }
            if (callback == null) throw new IllegalStateException("scan callback interface unavailable");
            IBinder.DeathRecipient death = new IBinder.DeathRecipient() {
                @Override public void binderDied() { unregisterScanCallback(binder); }
            };
            binder.linkToDeath(death, 0);
            scanCallbackBinders.add(binder);
            scanCallbacks.add(callback);
            scanCallbackDeaths.put(binder, death);
        }
    }

    private void unregisterScanCallback(IBinder binder) {
        synchronized (scanCallbacks) {
            int index = scanCallbackBinders.indexOf(binder);
            if (index < 0) return;
            scanCallbackBinders.remove(index);
            scanCallbacks.remove(index);
            IBinder.DeathRecipient death = scanCallbackDeaths.remove(binder);
            if (death != null) binder.unlinkToDeath(death, 0);
        }
    }

    /**
     * 21+: nothing sends the Wi-Fi state broadcasts any more (the framework's own state machine does, and it has no radio
     * here). Settings and the setup wizard follow them, so a reconnect that only moves the NetworkAgent is never "seen".
     */
    private final Runnable publishTask = new Runnable() {
        @Override public void run() {
            try {
                boolean en, con;
                synchronized (WifiService.this) { en = enabled; con = connected; }
                Intent ws = new Intent("android.net.wifi.WIFI_STATE_CHANGED");
                ws.addFlags(0x10000000);
                ws.putExtra("wifi_state", en ? 3 : 1);
                ws.putExtra("previous_wifi_state", en ? 3 : 1);
                Bcast.send(ws);

                Object ss = enumConst("android.net.wifi.SupplicantState", con ? "COMPLETED" : en ? "DISCONNECTED" : "INACTIVE");
                if (ss instanceof Parcelable) {
                    Intent si = new Intent("android.net.wifi.supplicant.STATE_CHANGE");
                    si.addFlags(0x10000000);
                    si.putExtra("newState", (Parcelable) ss);
                    Bcast.send(si);
                }

                Intent ns = new Intent("android.net.wifi.STATE_CHANGE");
                ns.addFlags(0x10000000);
                Object ni = Refl.create("android.net.NetworkInfo", new Class<?>[] { int.class, int.class, String.class, String.class },
                        Integer.valueOf(1), Integer.valueOf(0), "WIFI", "");
                if (ni != null) {
                    Refl.call(ni, "setDetailedState", enumConst("android.net.NetworkInfo$DetailedState", con ? "CONNECTED" : "DISCONNECTED"),
                            null, null);
                    Refl.call(ni, "setIsAvailable", Boolean.valueOf(en));
                    if (ni instanceof Parcelable) ns.putExtra("networkInfo", (Parcelable) ni);
                }
                Bcast.send(ns);
                Refl.log("state broadcasts: enabled=" + en + " connected=" + con);
            } catch (Throwable t) {
                Refl.log("state broadcasts failed: " + t);
            }
        }
    };

    private static Object enumConst(String cls, String name) {
        try {
            return Class.forName(cls).getField(name).get(null);
        } catch (Throwable t) {
            return null;
        }
    }

    /** startScan() finished: tell the guest that results are ready (broadcast and, on 11+, the registered callbacks). */
    private void scheduleScanDone() {
        scanHandler.postDelayed(new Runnable() {
            @Override public void run() {
                Intent i = new Intent("android.net.wifi.SCAN_RESULTS");
                i.putExtra("resultsUpdated", true);
                i.addFlags(0x10000000); // FLAG_RECEIVER_FOREGROUND
                Bcast.send(i);
                List<Object> cbs;
                synchronized (scanCallbacks) { cbs = new ArrayList<Object>(scanCallbacks); }
                for (Object c : cbs) Refl.call(c, "onScanResultsAvailable");
                Refl.log("scan done: broadcast + " + cbs.size() + " callback(s)");
            }
        }, 400);
    }

    private synchronized void handle(String name, Parcel data, Parcel reply) {
        if (reply == null) return; // one-way call: nothing to answer
        reply.writeNoException();
        if (name.startsWith("addOrUpdateNetwork") || name.startsWith("addNetwork") || name.equals("enableNetwork")
                || name.equals("disableNetwork") || name.equals("removeNetwork") || name.equals("disconnect")
                || name.equals("reconnect") || name.equals("reassociate") || name.equals("connect")
                || name.equals("save") || name.equals("forget") || name.equals("setWifiEnabled")
                || name.equals("startScan") || name.toLowerCase().contains("suggestion")
                || name.toLowerCase().contains("networkrequest")) {
            Refl.log("call: " + name);
        }
        if (name.equals("getWifiEnabledState")) {
            reply.writeInt(enabled ? STATE_ENABLED : STATE_DISABLED);
        } else if (name.equals("getWifiApEnabledState")) {
            reply.writeInt(AP_STATE_DISABLED);
        } else if (name.equals("setWifiEnabled")) {
            // 4.4: (boolean), 6.0+: (String callingPackage, boolean)
            if (data.dataAvail() > 4) data.readString();
            enabled = data.readInt() != 0;
            update();
            reply.writeInt(1);
        } else if (name.equals("getConnectionInfo")) {
            Object wi = wifiInfo();
            writeParcelable(reply, wi instanceof Parcelable ? (Parcelable) wi : null);
        } else if (name.equals("getScanResults")) {
            List<Parcelable> list = new ArrayList<Parcelable>();
            Object r = enabled ? scanResult() : null;
            if (r instanceof Parcelable) list.add((Parcelable) r);
            if (enabled) for (WifiConfig.Neighbor n : cfg.neighbors) {
                Object o = scanResult(n.ssid, n.bssid, n.caps, n.rssi, n.frequency);
                if (o instanceof Parcelable) list.add((Parcelable) o);
            }
            Refl.log("getScanResults: answering " + list.size() + " entries");
            writeList("getScanResults", list, reply);
        } else if (name.equals("getConfiguredNetworks") || name.equals("getPrivilegedConfiguredNetworks")) {
            List<Parcelable> list = new ArrayList<Parcelable>();
            if (saved) list.add(savedConfig());
            writeList(name, list, reply);
        } else if (name.equals("getDhcpInfo")) {
            writeParcelable(reply, dhcpInfo());
        } else if (name.equals("addOrUpdateNetwork")) {
            WifiConfiguration c = readConfig(data);
            boolean ok = credentialsOk(c);
            Refl.log("addOrUpdateNetwork: ssid=" + (c == null ? "null" : c.SSID) + " psk=" + (c != null && c.preSharedKey != null) + " -> " + (ok ? "accepted" : "rejected (expected " + cfg.ssid + ")"));
            int id = ok ? NET_ID : nextId++;
            addedOk.put(Integer.valueOf(id), Boolean.valueOf(ok));
            reply.writeInt(id);
        } else if (name.equals("enableNetwork")) {
            int id = data.readInt();
            boolean ok = id == NET_ID && saved || Boolean.TRUE.equals(addedOk.get(Integer.valueOf(id)));
            if (ok) { saved = true; down = false; update(); }
            reply.writeInt(1);
        } else if (name.equals("removeNetwork")) {
            int id = data.readInt();
            if (id == NET_ID) { saved = false; update(); }
            addedOk.remove(Integer.valueOf(id));
            reply.writeInt(1);
        } else if (name.equals("disableNetwork")) {
            int id = data.readInt();
            if (id == NET_ID) { down = true; update(); }
            reply.writeInt(1);
        } else if (name.equals("disconnect")) {
            down = true;
            update();
            reply.writeInt(1);
        } else if (name.equals("reconnect") || name.equals("reassociate")) {
            down = false;
            update();
            reply.writeInt(1);
        } else if (name.equals("isScanAlwaysAvailable")) {
            reply.writeInt(0); // The virtual AP cannot scan while Wi-Fi is disabled.
        } else if (name.equals("pingSupplicant") || name.equals("saveConfiguration")
                || name.equals("acquireWifiLock")) {
            reply.writeInt(1);
        } else if (name.equals("getPasspointConfigurations") || name.equals("getMatchingWifiConfig")
                || name.equals("getMatchingOsuProviders") || name.equals("getAllowedDevices")
                || name.equals("getHotspotClients")) {
            // no Passpoint profiles, OSU providers or hotspot clients here. 8.1 Settings walks getPasspointConfigurations()
            // without a null check, so these must be real empty collections, not null.
            writeEmpty(name, reply);
        } else if (name.equals("getSupportedFeatures")) {
            // WIFI_FEATURE_INFRA (0x1): plain infrastructure mode only. A long on 8+, an int before.
            Method fm = methods.get(name);
            if (fm != null && fm.getReturnType() == long.class) reply.writeLong(1L); else reply.writeInt(1);
        } else if (name.equals("reportActivityInfo")) {
            writeParcelable(reply, null); // no controller energy counters to report
        } else if (name.equals("requestActivityInfo")) {
            // the ResultReceiver gets an empty bundle: the caller treats it as "no data"
            try {
                if (data.readInt() != 0) {
                    android.os.ResultReceiver rr = android.os.ResultReceiver.CREATOR.createFromParcel(data);
                    rr.send(0, new android.os.Bundle());
                }
            } catch (Throwable t) {
                Refl.log("requestActivityInfo: " + t);
            }
        } else if (name.equals("stopSoftAp") || name.equals("stopLocalOnlyHotspot") || name.equals("enableAggressiveHandover")) {
            reply.writeInt(1);
        } else if (name.equals("startSoftAp") || name.equals("setWifiApEnabled")) {
            writeDefaultBool(name, reply); // there is no AP mode
        } else if (name.equals("getVerboseLoggingLevel") || name.equals("getAggressiveHandover")) {
            reply.writeInt(0);
        } else if (name.equals("getCurrentNetwork")) {
            writeParcelable(reply, null); // no Network handle is exposed for the emulated link
        } else if (name.equals("registerScanResultsCallback") || name.equals("unregisterScanResultsCallback")) {
            // 11+: the guest's WifiManager waits for onScanResultsAvailable() after startScan()
            IBinder b = data.readStrongBinder();
            if (b != null) {
                if (name.startsWith("register")) {
                    try {
                        Class<?> cb = Class.forName("android.net.wifi.IScanResultsCallback$Stub");
                        Object o = cb.getMethod("asInterface", IBinder.class).invoke(null, b);
                        synchronized (scanCallbacks) { scanCallbackBinders.add(b); scanCallbacks.add(o); }
                        Refl.log("registerScanResultsCallback: " + scanCallbacks.size() + " listener(s)");
                    } catch (Throwable t) {
                        Refl.log("registerScanResultsCallback: " + t);
                    }
                } else {
                    synchronized (scanCallbacks) {
                        int i = scanCallbackBinders.indexOf(b);
                        if (i >= 0) { scanCallbackBinders.remove(i); scanCallbacks.remove(i); }
                    }
                }
            }
        } else if (name.equals("startScan")) {
            scheduleScanDone();
            LegacyWifi lg = legacy;
            if (lg != null) lg.scanDone();
            Method sm = methods.get(name);
            if (sm != null && sm.getReturnType() == boolean.class) reply.writeInt(1); // 10+: boolean startScan(...)
        } else if (name.toLowerCase().contains("messenger")) {
            // 4.0 hands WifiManager the state machine's messenger (getWifiStateMachineMessenger), 4.1+ the service's
            writeParcelable(reply, messenger);
        } else if ((name.equals("connect") || name.equals("save") || name.equals("forget")) && listenerCall(name, data)) {
            // answered through the caller's IActionListener (10+)
        } else {
            if (unhandled.add(name)) Refl.log("not implemented, answering 0/null: " + name);
            writeDefault(name, reply);
        }
    }

    /**
     * The answer of a method this service does not implement, in the type the guest's own interface returns: a boolean or an int
     * is 0, a long is 0L, a String, an array, a list or a parcelable is null, a binder is null. Writing one int for everything (as
     * before) shifts the reader of a long and makes a null binder unreadable: 8+ asks for getSupportedFeatures() (a long) and
     * many callbacks registrations first thing.
     */
    private void writeDefault(String name, Parcel reply) {
        Method m = methods.get(name);
        Class<?> t = m == null ? int.class : m.getReturnType();
        if (t == void.class) return;
        if (t == long.class) reply.writeLong(0L);
        else if (t == float.class) reply.writeFloat(0f);
        else if (t == double.class) reply.writeDouble(0d);
        else if (t == boolean.class || t == int.class || t == short.class || t == byte.class || t == char.class) reply.writeInt(0);
        else if (t == String.class) reply.writeString(null);
        else if (t == CharSequence.class) reply.writeInt(0);
        else if (t.isArray()) reply.writeInt(-1);
        else if (List.class.isAssignableFrom(t)) reply.writeInt(-1);
        else if (IBinder.class.isAssignableFrom(t) || IInterface.class.isAssignableFrom(t)) reply.writeStrongBinder(null);
        else reply.writeInt(0); // a nullable parcelable (or anything else the old way)
    }

    /**
     * Android 10+ (and 8 with the same generated names): connect / save / forget do not use the messenger channel but take
     * an IActionListener. The parameters are read in the order the guest's interface declares them (a WifiConfiguration, the
     * network id, a binder token, the listener, a callback id: it differs between 10, 11 and 12+).
     * Returns false when the method has no listener parameter (the messenger channel of 4.x - 9 is used then).
     */
    private boolean listenerCall(String name, Parcel data) {
        Method m = methods.get(name);
        Class<?> al = Refl.cls("android.net.wifi.IActionListener");
        if (m == null || al == null) return false;
        Class<?>[] pt = m.getParameterTypes();
        boolean has = false;
        for (Class<?> p : pt) if (p == al) has = true;
        if (!has) return false;
        WifiConfiguration c = null;
        int netId = -1;
        boolean haveId = false;
        Object listener = null;
        for (Class<?> p : pt) {
            if (p == WifiConfiguration.class) c = readConfig(data);
            else if (p == int.class) { int v = data.readInt(); if (!haveId) { netId = v; haveId = true; } }
            else if (p == boolean.class) data.readInt();
            else if (p == String.class) data.readString();
            else if (p == IBinder.class) data.readStrongBinder();
            else if (p == al) {
                IBinder b = data.readStrongBinder();
                try {
                    Class<?> stub = Class.forName(al.getName() + "$Stub");
                    listener = b == null ? null : stub.getMethod("asInterface", IBinder.class).invoke(null, b);
                } catch (Throwable t) {
                    Refl.log("IActionListener: " + t);
                }
            } else break;
        }
        Refl.log(name + ": config=" + (c == null ? "null" : c.SSID) + " netId=" + netId + " listener=" + (listener != null));
        boolean ok;
        if (name.equals("forget")) {
            if (netId == NET_ID) { saved = false; update(); }
            ok = true;
        } else {
            if (c != null) ok = credentialsOk(c);
            else ok = netId == NET_ID && saved || Boolean.TRUE.equals(addedOk.get(Integer.valueOf(netId)));
            if (ok) {
                saved = true;
                if (name.equals("connect")) down = false;
                update();
            }
        }
        if (listener != null) {
            Refl.log(name + ": " + (ok ? "onSuccess" : "onFailure"));
            if (ok) Refl.call(listener, "onSuccess");
            else Refl.call(listener, "onFailure", Integer.valueOf(0));
        }
        return true;
    }

    // ------------------------------------------------------------------ the AsyncChannel WifiManager.connect()/save()/forget() use

    private final class ChannelHandler extends Handler {
        private final int fullConnection;
        private final int fullyConnected;
        private final int connectNetwork;
        private final int saveNetwork;
        private final int forgetNetwork;
        private final Class<?> mgr = Refl.cls("android.net.wifi.WifiManager");
        private final Class<?> sm = Refl.cls("android.net.wifi.WifiStateMachine");

        /** First constant that exists under any of [names] in WifiManager or WifiStateMachine (names differ between 4.0 and 4.1+). */
        private int konst(int fallback, String... names) {
            for (String n : names) {
                for (Class<?> c : new Class<?>[] { mgr, sm }) {
                    if (c == null) continue;
                    int v = Refl.intConst(c, n, Integer.MIN_VALUE);
                    if (v != Integer.MIN_VALUE) return v;
                }
            }
            return fallback;
        }

        ChannelHandler(Looper looper) {
            super(looper);
            Class<?> ac = Refl.cls("com.android.internal.util.AsyncChannel");
            fullConnection = Refl.intConst(ac, "CMD_CHANNEL_FULL_CONNECTION", -1);
            fullyConnected = Refl.intConst(ac, "CMD_CHANNEL_FULLY_CONNECTED", -1);
            connectNetwork = konst(-1, "CONNECT_NETWORK", "CMD_CONNECT_NETWORK");
            saveNetwork = konst(-1, "SAVE_NETWORK", "CMD_SAVE_NETWORK");
            forgetNetwork = konst(-1, "FORGET_NETWORK", "CMD_FORGET_NETWORK");
            Refl.log("channel codes: connect=" + connectNetwork + " save=" + saveNetwork + " forget=" + forgetNetwork
                    + " full=" + fullConnection);
        }

        private void answer(Message in, int what, int arg1) {
            if (in.replyTo == null || what < 0) return;
            Message m = Message.obtain();
            m.what = what;
            m.arg1 = arg1;
            m.arg2 = in.arg2; // the key of the caller's ActionListener
            try {
                in.replyTo.send(m);
            } catch (Throwable t) {
                Refl.log("reply: " + t);
            }
        }

        @Override
        public void handleMessage(Message msg) {
            synchronized (WifiService.this) {
                handleLocked(msg);
            }
        }

        private void handleLocked(Message msg) {
            if (msg.what == fullConnection) {
                answer(msg, fullyConnected, 0);
                return;
            }
            boolean connect = msg.what == connectNetwork;
            boolean save = msg.what == saveNetwork;
            boolean forget = msg.what == forgetNetwork;
            if (!connect && !save && !forget) { Refl.log("channel message ignored: what=" + msg.what); return; }
            // the matching *_FAILED / *_SUCCEEDED codes follow the command in WifiManager (BASE + 1, +2, +3 ...)
            int failed = msg.what + 1;
            int succeeded = msg.what + 2;
            String tag = connect ? "CONNECT_NETWORK" : save ? "SAVE_NETWORK" : "FORGET_NETWORK";
            failed = konst(failed, tag + "_FAILED");
            succeeded = konst(succeeded, tag + "_SUCCEEDED");
            if (forget) {
                if (msg.arg1 == NET_ID) { saved = false; update(); }
                answer(msg, succeeded, 0);
                return;
            }
            boolean ok;
            if (msg.obj instanceof WifiConfiguration) ok = credentialsOk((WifiConfiguration) msg.obj);
            else ok = msg.arg1 == NET_ID && saved || Boolean.TRUE.equals(addedOk.get(Integer.valueOf(msg.arg1)));
            if (ok) {
                saved = true;
                if (connect) down = false;
                update();
                answer(msg, succeeded, 0);
            } else {
                answer(msg, failed, 0);
            }
        }
    }
}
