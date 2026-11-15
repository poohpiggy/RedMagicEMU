package app.aemu.core

/** Names of files shipped by older kernels, retained for parked-file restoration. */
internal object LegacyN64Names {
    fun parse(text: String): Set<String> = text.lineSequence().filter { it.isNotEmpty() }.toSet()
    fun kernel(current: Set<String>, historic: Set<String>): Set<String> = current + historic
    fun layer(kernel: Set<String>, historicGsi: Set<String>): Set<String> = kernel + historicGsi
}
