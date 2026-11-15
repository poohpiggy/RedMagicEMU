package app.aemu.stub;

import android.content.Context;
import android.content.Intent;
import android.net.NetworkInfo;

public final class WifiStateBroadcastsTest {
    private static void check(boolean value, String message) {
        if (!value) throw new AssertionError(message);
    }
    public static void main(String[] args) {
        Context context = new Context();
        WifiStateBroadcasts.publish(context, false, true, false, true);
        check(context.broadcasts.size() == 3, "switch, supplicant and network broadcasts");
        Intent state = context.broadcasts.get(0);
        check("android.net.wifi.WIFI_STATE_CHANGED".equals(state.action), "switch action");
        check(Integer.valueOf(1).equals(state.extras.get("previous_wifi_state")), "previous switch");
        check(Integer.valueOf(3).equals(state.extras.get("wifi_state")), "new switch");
        Intent network = context.broadcasts.get(2);
        NetworkInfo info = (NetworkInfo) network.extras.get("networkInfo");
        check(info != null && info.state == NetworkInfo.DetailedState.CONNECTED && info.available, "connected network");
        check(info.extraInfo == null && !network.extras.containsKey("bssid") && !network.extras.containsKey("wifiInfo"), "no sensitive extras");
        context.broadcasts.clear();
        WifiStateBroadcasts.publish(context, true, true, true, false);
        check(context.broadcasts.size() == 2, "disconnect does not repeat switch broadcast");
        info = (NetworkInfo) context.broadcasts.get(1).extras.get("networkInfo");
        check(info.state == NetworkInfo.DetailedState.DISCONNECTED && info.extraInfo == null, "disconnect details");
        context.broadcasts.clear();
        WifiStateBroadcasts.publish(context, true, true, false, false);
        check(context.broadcasts.isEmpty(), "unchanged state sends nothing");
        WifiStateBroadcasts.publishInitial(context, true, true);
        check(context.broadcasts.size() == 3, "initial connected state is announced");
        state = context.broadcasts.get(0);
        check(Integer.valueOf(4).equals(state.extras.get("previous_wifi_state")), "initial previous state is unknown");
        check(Integer.valueOf(3).equals(state.extras.get("wifi_state")), "initial enabled state");
        info = (NetworkInfo) context.broadcasts.get(2).extras.get("networkInfo");
        check(info.state == NetworkInfo.DetailedState.CONNECTED && info.available, "initial connected info");
        check(info.extraInfo == null && !context.broadcasts.get(2).extras.containsKey("wifiInfo"), "initial info is redacted");
        context.broadcasts.clear();
        WifiStateBroadcasts.publishInitial(context, false, false);
        check(context.broadcasts.size() == 3, "initial disabled state is announced");
        check(Integer.valueOf(1).equals(context.broadcasts.get(0).extras.get("wifi_state")), "initial disabled switch");
        info = (NetworkInfo) context.broadcasts.get(2).extras.get("networkInfo");
        check(info.state == NetworkInfo.DetailedState.DISCONNECTED && !info.available, "initial disconnected info");
        context.broadcasts.clear();
        try {
            WifiStateBroadcasts.publishInitial(context, false, true);
            throw new AssertionError("impossible state must fail");
        } catch (IllegalArgumentException expected) {
            check(context.broadcasts.isEmpty(), "invalid state publishes nothing");
        }
        WifiStateBroadcasts.publishConfigured(context, false);
        Intent configured = context.broadcasts.get(0);
        check("android.net.wifi.CONFIGURED_NETWORKS_CHANGE".equals(configured.action), "configuration action");
        check("android.permission.ACCESS_WIFI_STATE".equals(context.lastBroadcastPermission), "configuration permission");
        check(Boolean.TRUE.equals(configured.extras.get("multipleChanges")), "configuration multiple flag");
        check(Integer.valueOf(1).equals(configured.extras.get("changeReason")), "configuration removal");
        check(!configured.extras.containsKey("wifiConfiguration"), "configuration not disclosed");
        context.broadcasts.clear();
        WifiStateBroadcasts.publishConfigured(context, true);
        check(Integer.valueOf(0).equals(context.broadcasts.get(0).extras.get("changeReason")), "configuration addition");
        System.out.println("WifiStateBroadcastsTest passed");
    }
}
