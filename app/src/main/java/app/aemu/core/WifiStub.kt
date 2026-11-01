package app.aemu.core

import java.io.File

/**
 * Emulated Wi-Fi for the 2.x-3.x stack: what the guest's libhardware_legacy wifi.c and libnetutils dhcp_utils.c
 * expect from init, the driver, wpa_supplicant and dhcpcd. The guest's network is the phone's, so the "access
 * point" is always in range and the link is always up; the framework only needs to see a station associate and
 * get a lease, as on the SDK emulator's virtual Wi-Fi.
 *
 * - driver: wifi_load_driver insmods the module (ker 23 answers init_module) and sets wlan.driver.status itself.
 * - supplicant: wifi_start_supplicant sets ctl.start=wpa_supplicant and waits for init.svc.wpa_supplicant=running;
 *   wifi_connect_to_supplicant opens the control socket: /data/system/wpa_supplicant/<iface> when that directory
 *   exists (IFACE_DIR), else the reserved socket /dev/socket/wpa_<iface> (wpa_ctrl.c, ANDROID). Commands are
 *   datagrams answered to their sender; ATTACH makes the sender a monitor that gets "<N>EVENT" datagrams.
 * - dhcp: dhcp_do_request sets ctl.start=dhcpcd[_<iface>][:args], waits for init.svc.<that service>=running, then for
 *   dhcp.<iface>.result, and reads ipaddress/gateway/mask/dns1/dns2/server/leasetime (2.2-3.2 dhcp_utils.c).
 * Event formats: frameworks/base/wifi/java/android/net/wifi/WifiMonitor.java (2.3, 3.x); supplicant states are
 * ordinals of android.net.wifi.SupplicantState, which gained INTERFACE_DISABLED and AUTHENTICATING in 4.0 (API 14).
 */
class WifiStub(
    private val paths: VmPaths,
    private val api: Int,
    private val props: PropService,
    private val dns: Pair<String, String>,
    private val log: (String) -> Unit,
) {
    private val fds = ArrayList<Int>()
    private val monitors = LinkedHashSet<String>()
    @Volatile private var connected = false
    private val networks = sortedMapOf(0 to linkedMapOf("ssid" to "\"$SSID\"", "key_mgmt" to "NONE", "priority" to "0"))

    /** ctl.start / ctl.stop of a service whose part the stub plays; false = not ours */
    fun onCtl(start: Boolean, value: String): Boolean {
        if (!HostNative.available) return false
        val name = value.substringBefore(':')
        when {
            name == "wpa_supplicant" -> if (start) startSupplicant() else stopSupplicant(name)
            name == "dhcpcd" || name.startsWith("dhcpcd_") || name.startsWith("iprenew_") -> {
                val iface = name.substringAfter('_', "").ifEmpty { value.substringAfter(':', "").trim().split(' ').lastOrNull { it.isNotEmpty() } ?: iface() }
                if (start) lease(name, iface) else props.put("init.svc.$name", "stopped")
            }
            else -> return false
        }
        return true
    }

    private fun iface() = props.get("wifi.interface")?.ifEmpty { null } ?: "wlan0"

    private fun lease(svc: String, iface: String) {
        props.put("init.svc.$svc", "running")
        val p = "dhcp.$iface"
        props.put("$p.ipaddress", IP)
        props.put("$p.gateway", GW)
        props.put("$p.mask", "255.255.255.0")
        props.put("$p.prefixLength", "24")
        props.put("$p.dns1", dns.first)
        props.put("$p.dns2", dns.second)
        props.put("$p.server", GW)
        props.put("$p.leasetime", "86400")
        props.put("$p.reason", "BOUND")
        props.put("$p.result", "ok")
        log("wifi: dhcp lease $IP on $iface ($svc)")
    }

    @Synchronized private fun startSupplicant() {
        if (fds.isNotEmpty()) { props.put("init.svc.wpa_supplicant", "running"); return }
        val iface = iface()
        File(paths.root, "data/misc/wifi/sockets").mkdirs()
        val places = mutableListOf(paths.socket("wpa_$iface"))
        File(paths.root, "data/system/wpa_supplicant").takeIf { it.isDirectory }?.let { places += File(it, iface) }
        for (f in places) {
            val fd = HostNative.dgramBind(f.absolutePath)
            if (fd < 0) { log("wifi: bind ${f.path}: errno ${-fd}"); continue }
            fds += fd
            Thread({ serve(fd) }, "wifi-ctrl").apply { isDaemon = true }.start()
        }
        connected = false
        props.put("init.svc.wpa_supplicant", if (fds.isEmpty()) "stopped" else "running")
        log("wifi: supplicant on $iface (${fds.size} sockets)")
    }

    @Synchronized private fun stopSupplicant(name: String) {
        for (fd in fds) HostNative.dgramClose(fd)
        fds.clear()
        synchronized(monitors) { monitors.clear() }
        connected = false
        props.put("init.svc.$name", "stopped")
    }

    fun stop() = stopSupplicant("wpa_supplicant")

    private fun serve(fd: Int) {
        val buf = ByteArray(4096)
        val from = arrayOfNulls<String>(1)
        while (true) {
            val n = HostNative.dgramRecv(fd, buf, from)
            if (n <= 0) break
            val who = from[0].orEmpty()
            val cmd = String(buf, 0, n).trimEnd('\n', '\u0000')
            val reply = runCatching { answer(fd, cmd, who) }.getOrElse { "FAIL\n" }
            if (who.isNotEmpty() && reply != null) HostNative.dgramSend(fd, reply.toByteArray(), who)
        }
    }

    private fun state(s: String): Int = if (api < 14) OLD.indexOf(s) else NEW.indexOf(s)

    private fun event(fd: Int, e: String) {
        val m = synchronized(monitors) { monitors.toList() }
        for (w in m) if (HostNative.dgramSend(fd, "<3>$e".toByteArray(), w) < 0) synchronized(monitors) { monitors.remove(w) }
    }

    private fun associate(fd: Int) = Thread {
        Thread.sleep(300)
        if (connected) return@Thread
        event(fd, "CTRL-EVENT-SCAN-RESULTS ")
        event(fd, "CTRL-EVENT-STATE-CHANGE id=0 state=${state("ASSOCIATING")}")
        event(fd, "CTRL-EVENT-STATE-CHANGE id=0 state=${state("ASSOCIATED")}")
        connected = true
        event(fd, "CTRL-EVENT-STATE-CHANGE id=0 state=${state("COMPLETED")} BSSID=$BSSID")
        event(fd, "CTRL-EVENT-CONNECTED - Connection to $BSSID completed (auth) [id=0 id_str=]")
        log("wifi: associated with $SSID")
    }.start()

    private fun disassociate(fd: Int) {
        if (!connected) return
        connected = false
        event(fd, "CTRL-EVENT-DISCONNECTED - Disconnect event - remove keys")
        event(fd, "CTRL-EVENT-STATE-CHANGE id=-1 state=${state("DISCONNECTED")} BSSID=00:00:00:00:00:00")
    }

    /** wpa_supplicant ctrl_iface.c commands as the 2.x-3.x framework (WifiNative) sends them */
    private fun answer(fd: Int, cmd: String, who: String): String? {
        val w = cmd.split(' ', limit = 3)
        return when (w[0]) {
            "PING" -> "PONG\n"
            "ATTACH" -> { synchronized(monitors) { monitors += who }; associate(fd); "OK\n" }
            "DETACH" -> { synchronized(monitors) { monitors -= who }; "OK\n" }
            "SCAN" -> { Thread { Thread.sleep(200); event(fd, "CTRL-EVENT-SCAN-RESULTS ") }.start(); "OK\n" }
            "SCAN_RESULTS" -> "bssid / frequency / signal level / flags / ssid\n$BSSID\t2437\t-45\t[ESS]\t$SSID\n"
            "STATUS", "STATUS-VERBOSE" -> if (connected)
                "bssid=$BSSID\nssid=$SSID\nid=0\nmode=station\npairwise_cipher=NONE\ngroup_cipher=NONE\nkey_mgmt=NONE\n" +
                    "wpa_state=COMPLETED\nip_address=$IP\naddress=$MAC\n"
            else "wpa_state=DISCONNECTED\naddress=$MAC\n"
            "LIST_NETWORKS" -> buildString {
                append("network id / ssid / bssid / flags\n")
                synchronized(networks) {
                    for ((id, n) in networks) append("$id\t${n["ssid"].orEmpty().trim('"')}\tany\t${if (id == 0 && connected) "[CURRENT]" else ""}\n")
                }
            }
            "ADD_NETWORK" -> synchronized(networks) { val id = (networks.lastKey() ?: -1) + 1; networks[id] = linkedMapOf(); "$id\n" }
            "REMOVE_NETWORK" -> synchronized(networks) {
                val id = w.getOrNull(1)?.toIntOrNull()
                if (w.getOrNull(1) == "all") { networks.keys.retainAll(setOf(0)); "OK\n" }
                else if (id != null && id != 0 && networks.remove(id) != null) "OK\n" else if (id == 0) "OK\n" else "FAIL\n"
            }
            "SET_NETWORK" -> synchronized(networks) {
                val id = w.getOrNull(1)?.toIntOrNull(); val kv = w.getOrNull(2)?.split(' ', limit = 2)
                val n = id?.let { networks[it] }
                if (n == null || kv == null || kv.size < 2) "FAIL\n" else { if (id != 0) n[kv[0]] = kv[1]; "OK\n" }
            }
            "GET_NETWORK" -> synchronized(networks) {
                val v = w.getOrNull(1)?.toIntOrNull()?.let { networks[it] }?.get(w.getOrNull(2) ?: "")
                if (v == null) "FAIL\n" else "$v"
            }
            "SELECT_NETWORK", "ENABLE_NETWORK", "RECONNECT", "REASSOCIATE" -> {
                if (w.getOrNull(1).let { it == null || it == "0" || it == "all" }) associate(fd); "OK\n"
            }
            "DISABLE_NETWORK" -> { if (w.getOrNull(1) == "0" || w.getOrNull(1) == "all") disassociate(fd); "OK\n" }
            "DISCONNECT" -> { disassociate(fd); "OK\n" }
            "TERMINATE" -> {
                event(fd, "CTRL-EVENT-TERMINATING ")
                Thread { Thread.sleep(100); stopSupplicant("wpa_supplicant") }.start(); "OK\n"
            }
            "DRIVER" -> when (w.getOrNull(1)?.uppercase()) {
                "RSSI", "RSSI-APPROX" -> if (connected) "$SSID rssi -45\n" else "OK\n"
                "LINKSPEED" -> "LinkSpeed 54\n"
                "MACADDR" -> "Macaddr = $MAC\n"
                "SCAN-CHANNELS" -> "Scan-Channels = 11\n"
                "GETPOWER" -> "powermode = 0\n"
                "GETBAND" -> "Band 0\n"
                else -> "OK\n"
            }
            "SIGNAL_POLL" -> "RSSI=-45\nLINKSPEED=54\nNOISE=9999\nFREQUENCY=2437\n"
            "BLACKLIST", "SAVE_CONFIG", "AP_SCAN", "SET", "BSSID", "LOGON", "LOGOFF", "REATTACH", "LEVEL", "PMKSA" -> "OK\n"
            else -> "OK\n"
        }
    }

    companion object {
        const val SSID = "AEmu"
        const val BSSID = "02:00:00:00:01:00"
        const val MAC = "02:00:00:00:00:00"
        const val IP = "10.0.2.15"
        const val GW = "10.0.2.2"
        /** android.net.wifi.SupplicantState up to 3.2 / from 4.0 */
        private val OLD = listOf("DISCONNECTED", "INACTIVE", "SCANNING", "ASSOCIATING", "ASSOCIATED",
            "FOUR_WAY_HANDSHAKE", "GROUP_HANDSHAKE", "COMPLETED", "DORMANT", "UNINITIALIZED", "INVALID")
        private val NEW = listOf("DISCONNECTED", "INTERFACE_DISABLED", "INACTIVE", "SCANNING", "AUTHENTICATING",
            "ASSOCIATING", "ASSOCIATED", "FOUR_WAY_HANDSHAKE", "GROUP_HANDSHAKE", "COMPLETED", "DORMANT", "UNINITIALIZED", "INVALID")
    }
}
