package app.aemu.core

/** Launch preparation is gated until the target guest implementation has been verified. */
internal object TargetWifiLaunch {
    private const val NET_JAR = "/system/framework/redmagic-wifi-net.jar"
    private const val WIFI_JAR = "/system/framework/redmagic-wifi.jar"

    /** Failure is reported and must not be inferred from artifacts left by an earlier boot. */
    fun prepare(enabled: Boolean, action: () -> Unit, failure: (Exception) -> Unit): Boolean {
        if (!enabled) return false
        return try { action(); true } catch (e: Exception) { failure(e); false }
    }

    fun select(ready: Boolean, api: Int, model: String, abi: String, engine: Engine,
               networkJar: Boolean, wifiJar: Boolean, config: Boolean): Boolean =
        ready && TargetWifiArtifacts.applies(api, model, abi, engine) && networkJar && wifiJar && config

    fun networkService(enabled: Boolean, ethernetStandIn: Boolean): GuestService {
        val jar = if (enabled) NET_JAR else "/system/framework/aemu-stubs.jar"
        val args = when {
            enabled -> listOf("ethernet", "redmagic-control")
            ethernetStandIn -> listOf("ethernet")
            else -> emptyList()
        }
        return GuestService("aemu-net", listOf("/system/bin/app_process", "-Djava.class.path=$jar",
            "/system/bin", "app.aemu.stub.NetStub") + args, optional = true)
    }

    fun wifiService(): GuestService = GuestService("redmagic-wifi", listOf("/system/bin/app_process",
        "-Djava.class.path=$WIFI_JAR", "/system/bin", "app.aemu.stub.WifiStub"), optional = true)

    fun dnsVerifier(socket: String, hostPid: Int, hostUid: Int, epoch: Long): GuestService {
        require(socket.matches(Regex("[A-Za-z0-9_.-]{1,80}")) && hostPid > 0 && hostUid >= 0 && epoch > 0)
        return GuestService("redmagic-wifi-dns", listOf("/system/bin/app_process",
            "-Djava.class.path=$WIFI_JAR", "/system/bin", "app.aemu.stub.WifiDnsVerifier",
            "--dns-verifier", socket, hostPid.toString(), hostUid.toString(), epoch.toString()), optional = true)
    }
}
