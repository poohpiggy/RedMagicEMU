package app.aemu.stub;

/** Methods audited for the API 36 prototype; all other transactions fail closed. */
final class WifiServiceGuard {
    static boolean isStateQuery(String name) {
        return "getWifiEnabledState".equals(name) || "getWifiApEnabledState".equals(name)
                || "isScanAlwaysAvailable".equals(name);
    }
    static boolean isSignalLevel(String name) {
        return "calculateSignalLevel".equals(name);
    }
    static boolean isVerboseLoggingQuery(String name) {
        return "getVerboseLoggingLevel".equals(name);
    }
    static boolean isScanCallback(String name) {
        return "registerScanResultsCallback".equals(name) || "unregisterScanResultsCallback".equals(name);
    }
    static boolean isPrivilegedScan(String name) {
        return "startScan".equals(name) || "getScanResults".equals(name);
    }
    static boolean isConnectionInfo(String name) {
        return "getConnectionInfo".equals(name) || "getConnectionInfo2".equals(name);
    }
    static boolean isPrivilegedMutation(String name) {
        return "connect".equals(name) || "forget".equals(name) || "setWifiEnabled".equals(name)
                || "disconnect".equals(name) || "disconnect2".equals(name) || "reconnect".equals(name)
                || "enableNetwork".equals(name);
    }
    static boolean isConfiguredQuery(String name) {
        return "getConfiguredNetworks".equals(name) || "getConfiguredNetworks2".equals(name);
    }
    static boolean isPrivilegedConfiguredQuery(String name) {
        return "getPrivilegedConfiguredNetworks".equals(name);
    }
    static boolean isCurrentNetwork(String name) {
        return "getCurrentNetwork".equals(name);
    }
    static boolean isSuggestionMatch(String name) {
        return "getWifiConfigForMatchedNetworkSuggestionsSharedWithUser".equals(name);
    }
    static boolean isPasspointQuery(String name) {
        return "getPasspointConfigurations".equals(name);
    }
    private WifiServiceGuard() { }
}
