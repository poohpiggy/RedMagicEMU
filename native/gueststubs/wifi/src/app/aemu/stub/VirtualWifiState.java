package app.aemu.stub;

/**
 * Pure state model for one virtual access point. Host connectivity is an observation,
 * not a statement that Android's NetworkMonitor has validated the guest network.
 * Binder permissions, callbacks, persistence and platform-specific lifecycle belong
 * to the caller and are not implemented here.
 */
final class VirtualWifiState {
    enum Result { SUCCESS, DISABLED, NETWORK_NOT_FOUND, UNSUPPORTED_SECURITY }
    enum HostInternet { UNKNOWN, AVAILABLE, UNAVAILABLE }

    private final String ssid;
    private boolean enabled;
    private boolean connected;
    private boolean saved;
    private HostInternet hostInternet = HostInternet.UNKNOWN;

    /** Snapshot used by the network coordinator while holding this object's monitor. */
    static final class Snapshot {
        final boolean enabled, connected, saved;
        final HostInternet hostInternet;
        Snapshot(boolean enabled, boolean connected, boolean saved, HostInternet hostInternet) {
            this.enabled = enabled;
            this.connected = connected;
            this.saved = saved;
            this.hostInternet = hostInternet;
        }
    }

    synchronized Snapshot snapshot() { return new Snapshot(enabled, connected, saved, hostInternet); }
    synchronized void restore(Snapshot previous) {
        enabled = previous.enabled;
        connected = previous.connected;
        saved = previous.saved;
        hostInternet = previous.hostInternet;
    }

    VirtualWifiState(String ssid, String credential) {
        if (ssid == null || ssid.isEmpty() || credential == null || !credential.isEmpty()) {
            throw new IllegalArgumentException("SSID and open-network configuration are required");
        }
        this.ssid = ssid;
    }

    synchronized boolean isEnabled() { return enabled; }
    synchronized boolean isConnected() { return connected; }
    synchronized boolean hasSavedNetwork() { return saved; }
    synchronized HostInternet hostInternet() { return hostInternet; }

    synchronized void setHostInternet(HostInternet value) {
        if (value == null) throw new IllegalArgumentException("host status is required");
        hostInternet = value;
    }

    synchronized void setEnabled(boolean value) {
        enabled = value;
        if (!value) connected = false;
    }

    synchronized Result connect(String requestedSsid, String requestedCredential) {
        if (!enabled) return Result.DISABLED;
        if (!ssid.equals(requestedSsid)) return Result.NETWORK_NOT_FOUND;
        if (requestedCredential != null && !requestedCredential.isEmpty()) return Result.UNSUPPORTED_SECURITY;
        saved = true;
        connected = true;
        return Result.SUCCESS;
    }

    synchronized void disconnect() { connected = false; }

    synchronized Result reconnect() {
        if (!enabled) return Result.DISABLED;
        if (!saved) return Result.NETWORK_NOT_FOUND;
        connected = true;
        return Result.SUCCESS;
    }

    synchronized void forget() {
        connected = false;
        saved = false;
    }
}
