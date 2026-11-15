package app.aemu.stub;

import android.net.ConnectivityManager;
import android.net.LinkProperties;
import android.net.Network;
import android.net.NetworkCapabilities;

/** Checks the control lease and the system's view before Ethernet is stopped. */
final class WifiControlReadiness {
    private static final int ACTIVE = 0, UNAVAILABLE = 3;

    private WifiControlReadiness() { }

    static boolean isReady(int state, Network network, ConnectivityManager manager) {
        if (state == UNAVAILABLE) return false; // Control Binder is not armed yet.
        if (state != ACTIVE || network == null)
            throw new IllegalStateException("Ethernet control state is not active");
        if (manager == null) throw new IllegalStateException("guest connectivity unavailable");
        Network[] networks = manager.getAllNetworks();
        if (networks == null) throw new IllegalStateException("guest network list unavailable");
        for (Network current : networks) {
            if (!network.equals(current)) continue;
            NetworkCapabilities capabilities = manager.getNetworkCapabilities(network);
            LinkProperties link = manager.getLinkProperties(network);
            if (capabilities == null || link == null || link.getInterfaceName() == null) return false;
            if (!capabilities.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET)
                    || !"eth0".equals(link.getInterfaceName()))
                throw new IllegalStateException("Ethernet control network does not use eth0");
            return true;
        }
        return false; // Registration can propagate after the Binder is published.
    }
}
