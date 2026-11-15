package app.aemu.core

import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import java.io.Closeable
import java.util.concurrent.atomic.AtomicLong

internal fun targetHostAvailable(active: Any?, lost: Any?, validated: Boolean): Boolean =
    active != null && active != lost && validated

/** Tracks the host default used by the existing transport without changing host Wi-Fi. */
internal class TargetHostNetworks(private val cm: ConnectivityManager, private val availability: (Boolean, Boolean) -> Unit = { _, _ -> }) : TargetDnsResolver.HostNetworks, Closeable {
    private val generation = AtomicLong()
    @Volatile private var closed = false
    private val callback = object : ConnectivityManager.NetworkCallback() {
        override fun onAvailable(network: Network) { changed(force = true) }
        override fun onLost(network: Network) { changed(network, force = true) }
        override fun onCapabilitiesChanged(network: Network, caps: NetworkCapabilities) { changed() }
    }
    init { cm.registerDefaultNetworkCallback(callback); changed() }

    private fun changed(lost: Network? = null, force: Boolean = false) {
        generation.incrementAndGet()
        if (closed) return
        val validated = try {
            val current = cm.activeNetwork
            val caps = if (current == null || current == lost) null else cm.getNetworkCapabilities(current)
            targetHostAvailable(current, lost, caps?.hasCapability(NetworkCapabilities.NET_CAPABILITY_VALIDATED) == true)
        } catch (failure: Exception) {
            android.util.Log.w("WifiDnsHostNetwork", "Host network status unavailable", failure)
            return
        }
        availability(validated, force)
    }

    override fun snapshot(): TargetDnsResolver.HostNetwork? {
        if (closed) return null
        val before = generation.get()
        val network = cm.activeNetwork ?: return null
        val caps = cm.getNetworkCapabilities(network) ?: return null
        if (!caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET) || closed || before != generation.get()) return null
        return TargetDnsResolver.HostNetwork(network, before)
    }

    override fun isCurrent(snapshot: TargetDnsResolver.HostNetwork): Boolean {
        if (closed || snapshot.generation != generation.get()) return false
        val network = snapshot.network as? Network ?: return false
        if (cm.activeNetwork != network) return false
        val caps = cm.getNetworkCapabilities(network) ?: return false
        return caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET) && !closed && snapshot.generation == generation.get()
    }

    override fun close() {
        if (!closed) {
            closed = true
            generation.incrementAndGet()
            cm.unregisterNetworkCallback(callback)
        }
    }
}
