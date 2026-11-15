package app.aemu.stub;

public final class WifiServiceGuardTest {
    public static void main(String[] args) {
        if (!WifiServiceGuard.isStateQuery("getWifiEnabledState")) throw new AssertionError();
        if (!WifiServiceGuard.isStateQuery("getWifiApEnabledState")) throw new AssertionError();
        if (!WifiServiceGuard.isStateQuery("isScanAlwaysAvailable")) throw new AssertionError();
        if (!WifiServiceGuard.isSignalLevel("calculateSignalLevel")) throw new AssertionError();
        if (WifiServiceGuard.isSignalLevel(null) || WifiServiceGuard.isSignalLevel("getWifiEnabledState")) throw new AssertionError();
        if (!WifiServiceGuard.isVerboseLoggingQuery("getVerboseLoggingLevel")) throw new AssertionError();
        if (WifiServiceGuard.isVerboseLoggingQuery(null) || WifiServiceGuard.isVerboseLoggingQuery("setVerboseLoggingEnabled")) throw new AssertionError();
        if (!WifiServiceGuard.isSuggestionMatch("getWifiConfigForMatchedNetworkSuggestionsSharedWithUser")) throw new AssertionError();
        if (WifiServiceGuard.isSuggestionMatch("getPrivilegedConfiguredNetworks") || WifiServiceGuard.isSuggestionMatch(null)) throw new AssertionError();
        if (!WifiServiceGuard.isPasspointQuery("getPasspointConfigurations")) throw new AssertionError();
        if (WifiServiceGuard.isPasspointQuery("getPrivilegedConfiguredNetworks") || WifiServiceGuard.isPasspointQuery(null)) throw new AssertionError();
        if (!WifiServiceGuard.isScanCallback("registerScanResultsCallback")) throw new AssertionError();
        if (!WifiServiceGuard.isScanCallback("unregisterScanResultsCallback")) throw new AssertionError();
        if (!WifiServiceGuard.isPrivilegedScan("startScan")) throw new AssertionError();
        if (!WifiServiceGuard.isPrivilegedScan("getScanResults")) throw new AssertionError();
        if (!WifiServiceGuard.isConnectionInfo("getConnectionInfo")) throw new AssertionError();
        if (!WifiServiceGuard.isConnectionInfo("getConnectionInfo2")) throw new AssertionError();
        if (!WifiServiceGuard.isPrivilegedMutation("connect")) throw new AssertionError();
        if (!WifiServiceGuard.isPrivilegedMutation("forget")) throw new AssertionError();
        if (!WifiServiceGuard.isPrivilegedMutation("setWifiEnabled")) throw new AssertionError();
        for (String name : new String[] { "getConnectionInfo", "getConnectionInfo2", "getScanResults",
                "connect", "forget", "setWifiEnabled", "startScan", "getCurrentNetwork", "unknown" }) {
            if (WifiServiceGuard.isStateQuery(name)) throw new AssertionError("unverified method: " + name);
            if (WifiServiceGuard.isScanCallback(name)) throw new AssertionError("unverified callback: " + name);
            if (WifiServiceGuard.isPrivilegedScan(name) && !name.equals("startScan") && !name.equals("getScanResults")) throw new AssertionError("unverified scan: " + name);
            if (WifiServiceGuard.isConnectionInfo(name) && !name.equals("getConnectionInfo") && !name.equals("getConnectionInfo2")) throw new AssertionError("unverified info: " + name);
        }
        if (WifiServiceGuard.isStateQuery(null)) throw new AssertionError("null");
        System.out.println("WifiServiceGuardTest passed");
    }
}
