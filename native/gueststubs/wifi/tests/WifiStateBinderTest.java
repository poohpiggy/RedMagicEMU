package app.aemu.stub;

import android.content.Context;
import android.app.AppOpsManager;
import android.net.Network;
import android.os.IBinder;
import android.os.Parcel;

public final class WifiStateBinderTest {
    private static void check(boolean value, String message) {
        if (!value) throw new AssertionError(message);
    }

    public static void main(String[] args) throws Exception {
        Context context = new Context();
        VirtualWifiState state = new VirtualWifiState("RedMagicEMU", "");
        WifiStateBinder binder = new WifiStateBinder(context, state);
        Parcel reply = new Parcel();
        check(binder.onTransact(IBinder.INTERFACE_TRANSACTION, new Parcel(), reply, 0), "descriptor transaction");
        check("android.net.wifi.IWifiManager".equals(reply.text), "descriptor");
        check(!binder.onTransact(123456, new Parcel(), new Parcel(), 0), "unknown method not handled");
        int transaction = android.net.wifi.IWifiManager.Stub.TRANSACTION_getWifiEnabledState;
        boolean denied = false;
        try { binder.onTransact(transaction, new Parcel("android.net.wifi.IWifiManager"), new Parcel(), 0); }
        catch (SecurityException expected) { denied = true; }
        check(denied, "permission enforced");
        context.allowed = true;
        denied = false;
        try { binder.onTransact(transaction, new Parcel("wrong"), new Parcel(), 0); }
        catch (SecurityException expected) { denied = true; }
        check(denied, "interface token enforced");
        Parcel extra = new Parcel("android.net.wifi.IWifiManager");
        extra.hasExtraData = true;
        boolean malformed = false;
        try { binder.onTransact(transaction, extra, new Parcel(), 0); }
        catch (IllegalArgumentException expected) { malformed = true; }
        check(malformed, "unexpected data rejected");
        reply = new Parcel();
        check(binder.onTransact(transaction, new Parcel("android.net.wifi.IWifiManager"), reply, 0), "query disabled");
        check(reply.success && reply.number == 1, "disabled result");
        state.setEnabled(true);
        reply = new Parcel();
        check(binder.onTransact(transaction, new Parcel("android.net.wifi.IWifiManager"), reply, 0), "query enabled");
        check(reply.success && reply.number == 3, "enabled result");
        check(!binder.onTransact(transaction, new Parcel("android.net.wifi.IWifiManager"), null, 0), "missing reply not reported successful");
        int toggle = android.net.wifi.IWifiManager.Stub.TRANSACTION_setWifiEnabled;
        AppOpsManager appOps = new AppOpsManager();
        context.appOps = appOps;
        denied = false;
        try { binder.onTransact(toggle, new Parcel("android.net.wifi.IWifiManager", "spoofed.package", false), new Parcel(), 0); }
        catch (SecurityException expected) { denied = true; }
        check(denied && state.isEnabled(), "reject spoofed package without changing state");
        denied = false;
        try { binder.onTransact(toggle, new Parcel("android.net.wifi.IWifiManager", "valid.package", false), new Parcel(), 0); }
        catch (SecurityException expected) { denied = true; }
        check(denied && state.isEnabled(), "reject missing CHANGE_WIFI_STATE");
        context.changeWifiState = true;
        appOps.mode = 1;
        reply = new Parcel();
        check(binder.onTransact(toggle, new Parcel("android.net.wifi.IWifiManager", "valid.package", false), reply, 0), "ignored app-op");
        check(reply.success && reply.number == 0 && state.isEnabled(), "ignored app-op refused");
        appOps.mode = 2;
        denied = false;
        try { binder.onTransact(toggle, new Parcel("android.net.wifi.IWifiManager", "valid.package", false), new Parcel(), 0); }
        catch (SecurityException expected) { denied = true; }
        check(denied && state.isEnabled(), "errored app-op propagates exception");
        appOps.mode = AppOpsManager.MODE_ALLOWED;
        reply = new Parcel();
        check(binder.onTransact(toggle, new Parcel("android.net.wifi.IWifiManager", "valid.package", false), reply, 0), "authorized caller");
        check(reply.success && reply.number == 0 && state.isEnabled(), "incomplete transition must return false");
        int before = appOps.notes;
        context.networkSettings = true;
        context.changeWifiState = false;
        reply = new Parcel();
        check(binder.onTransact(toggle, new Parcel("android.net.wifi.IWifiManager", "valid.package", false), reply, 0), "settings caller");
        check(appOps.notes == before && reply.number == 0 && state.isEnabled(), "settings exemption and incomplete transition");
        context.appOps = null;
        boolean unavailable = false;
        try { binder.onTransact(toggle, new Parcel("android.net.wifi.IWifiManager", "valid.package", false), new Parcel(), 0); }
        catch (IllegalStateException expected) { unavailable = true; }
        check(unavailable && state.isEnabled(), "missing app-ops fails closed");
        int currentNetwork = android.net.wifi.IWifiManager.Stub.TRANSACTION_getCurrentNetwork;
        context.networkSettings = false;
        denied = false;
        try { binder.onTransact(currentNetwork, new Parcel("android.net.wifi.IWifiManager"), new Parcel(), 0); }
        catch (SecurityException expected) { denied = true; }
        check(denied, "current network requires Settings or setup wizard");
        context.networkSetupWizard = true;
        reply = new Parcel();
        check(binder.onTransact(currentNetwork, new Parcel("android.net.wifi.IWifiManager"), reply, 0), "disconnected current network");
        check(reply.success && reply.typedObject == null, "no connection has no current network");
        check(state.connect("RedMagicEMU", "") == VirtualWifiState.Result.SUCCESS, "connect test model");
        boolean missingNetwork = false;
        try { binder.onTransact(currentNetwork, new Parcel("android.net.wifi.IWifiManager"), new Parcel(), 0); }
        catch (IllegalStateException expected) { missingNetwork = true; }
        check(missingNetwork, "connected without registered network must not claim disconnected");
        final Network active = new Network();
        binder = new WifiStateBinder(context, state, new WifiStateBinder.CurrentNetworkSource() {
            public Network get() { return active; }
        });
        reply = new Parcel();
        check(binder.onTransact(currentNetwork, new Parcel("android.net.wifi.IWifiManager"), reply, 0), "current network available");
        check(reply.success && reply.typedObject == active, "return active network");
        System.out.println("WifiStateBinderTest passed");
    }
}
