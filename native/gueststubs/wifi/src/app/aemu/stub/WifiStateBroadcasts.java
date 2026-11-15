package app.aemu.stub;

import android.content.Context;
import android.content.Intent;
import android.net.NetworkInfo;
import android.net.wifi.SupplicantState;

/** Publishes state without including the AP identity or an unredacted WifiInfo. */
final class WifiStateBroadcasts {
    /** Announce the confirmed service state without inventing the previous service state. */
    static void publishInitial(Context context, boolean enabled, boolean connected) {
        if (connected && !enabled) throw new IllegalArgumentException("disabled Wi-Fi cannot be connected");
        try { publishWifiState(context, 4 /* WIFI_STATE_UNKNOWN */, enabled); }
        catch (RuntimeException failure) { throw new InitialBroadcastFailure("wifi_state", failure); }
        try { publishSupplicantState(context, enabled, connected); }
        catch (RuntimeException failure) { throw new InitialBroadcastFailure("supplicant", failure); }
        try { publishNetworkInfo(context, enabled, connected); }
        catch (RuntimeException failure) { throw new InitialBroadcastFailure("network_state", failure); }
    }

    static void publish(Context context, boolean previousEnabled, boolean enabled,
            boolean previousConnected, boolean connected) {
        if (previousEnabled != enabled) {
            publishWifiState(context, previousEnabled ? 3 : 1, enabled);
        }
        if (previousEnabled == enabled && previousConnected == connected) return;
        publishNetworkState(context, enabled, connected);
    }

    static final class InitialBroadcastFailure extends IllegalStateException {
        final String stage;
        InitialBroadcastFailure(String stage, RuntimeException cause) { super(cause); this.stage = stage; }
    }

    /** API 36 omits the configuration itself and protects this broadcast. */
    static void publishConfigured(Context context, boolean saved) {
        Intent intent = new Intent("android.net.wifi.CONFIGURED_NETWORKS_CHANGE");
        intent.putExtra("multipleChanges", true);
        intent.putExtra("changeReason", saved ? 0 : 1);
        context.sendBroadcast(intent, "android.permission.ACCESS_WIFI_STATE");
    }

    private static void publishWifiState(Context context, int previous, boolean enabled) {
        Intent wifi = new Intent("android.net.wifi.WIFI_STATE_CHANGED");
        wifi.putExtra("previous_wifi_state", previous);
        wifi.putExtra("wifi_state", enabled ? 3 : 1);
        context.sendBroadcast(wifi);
    }

    private static void publishNetworkState(Context context, boolean enabled, boolean connected) {
        publishSupplicantState(context, enabled, connected);
        publishNetworkInfo(context, enabled, connected);
    }

    private static void publishSupplicantState(Context context, boolean enabled, boolean connected) {
        Intent supplicant = new Intent("android.net.wifi.supplicant.STATE_CHANGE");
        supplicant.putExtra("newState", (android.os.Parcelable) (connected ? SupplicantState.COMPLETED
                : enabled ? SupplicantState.DISCONNECTED : SupplicantState.INACTIVE));
        context.sendBroadcast(supplicant);
    }

    private static void publishNetworkInfo(Context context, boolean enabled, boolean connected) {
        NetworkInfo info = new NetworkInfo(1, 0, "WIFI", "");
        info.setDetailedState(connected ? NetworkInfo.DetailedState.CONNECTED
                : NetworkInfo.DetailedState.DISCONNECTED, null, null);
        try {
            info.getClass().getMethod("setIsAvailable", boolean.class).invoke(info, enabled);
        } catch (ReflectiveOperationException failure) {
            throw new IllegalStateException("network availability unavailable", failure);
        }
        Intent network = new Intent("android.net.wifi.STATE_CHANGE");
        network.putExtra("networkInfo", info);
        context.sendBroadcast(network);
    }
    private WifiStateBroadcasts() { }
}
