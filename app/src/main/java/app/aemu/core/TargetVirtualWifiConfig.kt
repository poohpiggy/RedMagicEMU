package app.aemu.core

/** Configuration for the target's virtual access point; not a host Wi-Fi setting. */
data class TargetVirtualWifiConfig(
    val ssid: String = "RedMagicEMU",
    val rssiDbm: Int = -45,
) {
    fun isValid(): Boolean {
        val utf8 = Charsets.UTF_8.newEncoder()
        if (!utf8.canEncode(ssid) || ssid.any { Character.isISOControl(it) } || ssid.toByteArray(Charsets.UTF_8).size !in 1..32) return false
        return rssiDbm in -100..-30
    }
}
