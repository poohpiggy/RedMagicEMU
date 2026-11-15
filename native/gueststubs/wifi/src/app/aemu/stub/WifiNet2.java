/* Modified for AEmulator Plus, 2026-11-01: Android 11 - 17. GPL-3.0; see LICENSE and NOTICE.md.
 * Modified for RedMagicEMU, 2026-11-10: use the API 36 agent and propagate unregister failures. */
package app.aemu.stub;

import android.content.Context;
import android.net.LinkProperties;
import android.net.Network;
import android.os.Looper;

final class WifiNet2 implements WifiLink {
    private final VirtualWifiNetworkAgent agent;
    private WifiNet2(VirtualWifiNetworkAgent agent) { this.agent = agent; }

    static WifiNet2 start(Context context, Looper looper, LinkProperties link, Object wifiInfo,
            Runnable onUnwanted) throws Exception {
        return new WifiNet2(VirtualWifiNetworkAgent.start(context, looper, link, wifiInfo, onUnwanted));
    }

    Network network() { return agent.network(); }

    @Override public void disconnect() {
        try { agent.stop(); }
        catch (Exception failure) { throw new IllegalStateException("Wi-Fi network removal failed", failure); }
    }
}
