package app.aemu.stub;

/**
 * Reconciles the virtual association and the network agent as one operation.
 * All association mutations must go through this coordinator. A factory that
 * fails to start must remove any agent it has already registered.
 */
final class WifiConnectionCoordinator {
    private interface Mutation { VirtualWifiState.Result run(); }
    private final VirtualWifiState state;
    private final WifiNetworkLifecycle network;

    WifiConnectionCoordinator(VirtualWifiState state, WifiNetworkLifecycle network) {
        if (state == null || network == null) throw new IllegalArgumentException("state and network are required");
        this.state = state;
        this.network = network;
    }

    /** UNKNOWN must be resolved externally before reporting a registered network or retrying. */
    WifiNetworkLifecycle.Status networkStatus() { return network.status(); }

    private VirtualWifiState.Result apply(Mutation mutation) throws Exception {
        synchronized (state) {
            VirtualWifiState.Snapshot previous = state.snapshot();
            VirtualWifiState.Result result = mutation.run();
            boolean connected = state.isConnected();
            try {
                // Close before removing the agent and open only after registration succeeds.
                if (!connected) WifiNetworkGate.setConnected(false);
                network.setConnected(connected);
                if (connected) WifiNetworkGate.setConnected(true);
            } catch (Exception | Error failure) {
                state.restore(previous);
                if (connected) {
                    try {
                        network.setConnected(false);
                        WifiNetworkGate.setConnected(false);
                    } catch (Exception | Error cleanupFailure) {
                        failure.addSuppressed(cleanupFailure);
                    }
                }
                throw failure;
            }
            return result;
        }
    }

    void setEnabled(final boolean enabled) throws Exception {
        apply(new Mutation() { public VirtualWifiState.Result run() { state.setEnabled(enabled); return null; } });
    }

    VirtualWifiState.Result connect(final String ssid, final String credential) throws Exception {
        return apply(new Mutation() { public VirtualWifiState.Result run() { return state.connect(ssid, credential); } });
    }

    void disconnect() throws Exception {
        apply(new Mutation() { public VirtualWifiState.Result run() { state.disconnect(); return null; } });
    }

    VirtualWifiState.Result reconnect() throws Exception {
        return apply(new Mutation() { public VirtualWifiState.Result run() { return state.reconnect(); } });
    }

    void forget() throws Exception {
        apply(new Mutation() { public VirtualWifiState.Result run() { state.forget(); return null; } });
    }

    void setHostInternet(VirtualWifiState.HostInternet status) {
        state.setHostInternet(status);
    }
}
