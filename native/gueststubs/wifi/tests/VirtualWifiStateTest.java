package app.aemu.stub;

public final class VirtualWifiStateTest {
    private static void check(boolean value, String message) {
        if (!value) throw new AssertionError(message);
    }

    public static void main(String[] args) {
        boolean rejected = false;
        try { new VirtualWifiState("RedMagicEMU", "password"); } catch (IllegalArgumentException expected) { rejected = true; }
        check(rejected, "secured network configuration rejected");
        VirtualWifiState wifi = new VirtualWifiState("RedMagicEMU", "");
        check(!wifi.isEnabled() && !wifi.isConnected(), "initially disabled");
        check(wifi.hostInternet() == VirtualWifiState.HostInternet.UNKNOWN, "host status starts unknown");
        check(wifi.connect("RedMagicEMU", "") == VirtualWifiState.Result.DISABLED, "disabled connect");
        wifi.setEnabled(true);
        check(wifi.connect("RedMagicEMU", "password") == VirtualWifiState.Result.UNSUPPORTED_SECURITY, "secured connection rejected");
        check(!wifi.isConnected() && !wifi.hasSavedNetwork(), "rejected connection must not save");
        check(wifi.connect("other", "") == VirtualWifiState.Result.NETWORK_NOT_FOUND, "unknown SSID");
        check(wifi.connect("RedMagicEMU", "") == VirtualWifiState.Result.SUCCESS, "connect");
        check(wifi.isConnected() && wifi.hasSavedNetwork(), "connected and saved");
        check(wifi.connect("RedMagicEMU", "password") == VirtualWifiState.Result.UNSUPPORTED_SECURITY, "secured request while connected");
        check(wifi.isConnected() && wifi.hasSavedNetwork(), "failed attempt preserves current connection");
        wifi.setHostInternet(VirtualWifiState.HostInternet.UNAVAILABLE);
        check(wifi.isConnected() && wifi.hostInternet() == VirtualWifiState.HostInternet.UNAVAILABLE, "outage does not disconnect AP");
        wifi.setHostInternet(VirtualWifiState.HostInternet.AVAILABLE);
        check(wifi.isConnected() && wifi.hostInternet() == VirtualWifiState.HostInternet.AVAILABLE, "host recovery");
        wifi.disconnect();
        check(!wifi.isConnected() && wifi.hasSavedNetwork(), "disconnect preserves network");
        check(wifi.reconnect() == VirtualWifiState.Result.SUCCESS, "reconnect saved network");
        wifi.setEnabled(false);
        check(!wifi.isConnected() && wifi.hasSavedNetwork(), "disable preserves network");
        wifi.setEnabled(true);
        check(wifi.reconnect() == VirtualWifiState.Result.SUCCESS, "reconnect after enable");
        wifi.forget();
        check(!wifi.isConnected() && !wifi.hasSavedNetwork(), "forget removes and disconnects");
        check(wifi.reconnect() == VirtualWifiState.Result.NETWORK_NOT_FOUND, "forgotten network cannot reconnect");

        VirtualWifiState open = new VirtualWifiState("Open", "");
        open.setEnabled(true);
        check(open.connect("Open", "") == VirtualWifiState.Result.SUCCESS, "open network");
        check(open.connect("Open", "unexpected") == VirtualWifiState.Result.UNSUPPORTED_SECURITY, "open network rejects secured request");
        System.out.println("VirtualWifiStateTest passed");
    }
}
