package app.aemu.stub;

import android.content.Context;
import android.net.LinkProperties;
import android.net.Network;
import android.net.NetworkAgent;
import android.net.NetworkAgentConfig;
import android.net.NetworkCapabilities;
import android.os.Looper;

import java.lang.reflect.Constructor;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;

/**
 * Android 16 network-agent adapter. Link properties are supplied by the caller
 * from the existing guest route, including its real interface and DNS servers.
 * NetworkMonitor, not this agent, determines whether internet is validated.
 */
final class VirtualWifiNetworkAgent extends NetworkAgent implements WifiNetworkLifecycle.Connection {
    private final Runnable onUnwanted;
    private Network network;

    private VirtualWifiNetworkAgent(Context context, Looper looper, NetworkCapabilities capabilities,
            LinkProperties link, NetworkAgentConfig config, Runnable onUnwanted) {
        super(context, looper, "RedMagicWifi", capabilities, link, 60, config, null);
        this.onUnwanted = onUnwanted;
    }

    @Override protected void unwanted() {
        if (onUnwanted != null) onUnwanted.run();
    }

    // Android 16 delivers the modern public callback. The compile-only stub
    // predates it, so this deliberately does not use @Override.
    public void onNetworkUnwanted() {
        if (onUnwanted != null) onUnwanted.run();
    }

    Network network() { return network; }

    @Override public void stop() throws Exception {
        call(this, "unregister", new Class<?>[0]);
    }

    static VirtualWifiNetworkAgent start(Context context, Looper looper, LinkProperties link,
            Object wifiInfo, Runnable onUnwanted) throws Exception {
        if (context == null || looper == null || link == null || link.getInterfaceName() == null
                || link.getInterfaceName().isEmpty()) {
            throw new IllegalArgumentException("context, looper and existing interface are required");
        }

        Constructor<NetworkCapabilities> constructor = NetworkCapabilities.class.getDeclaredConstructor();
        constructor.setAccessible(true);
        NetworkCapabilities capabilities = constructor.newInstance();
        call(capabilities, "addTransportType", new Class<?>[] { int.class }, 1 /* WIFI */);
        // INTERNET does not assert reachability; NetworkMonitor manages VALIDATED dynamically.
        for (int capability : new int[] { 12, 13, 14, 15, 11, 18, 20, 21, 28 }) {
            call(capabilities, "addCapability", new Class<?>[] { int.class }, capability);
        }
        if (wifiInfo != null) {
            Class<?> transportInfo = Class.forName("android.net.TransportInfo");
            call(capabilities, "setTransportInfo", new Class<?>[] { transportInfo }, wifiInfo);
        }

        Class<?> builderClass = Class.forName("android.net.NetworkAgentConfig$Builder");
        Object builder = builderClass.getDeclaredConstructor().newInstance();
        call(builder, "setLegacyType", new Class<?>[] { int.class }, 1 /* TYPE_WIFI */);
        call(builder, "setLegacyTypeName", new Class<?>[] { String.class }, "WIFI");
        NetworkAgentConfig config = (NetworkAgentConfig) call(builder, "build", new Class<?>[0]);
        if (config == null) throw new IllegalStateException("network agent config unavailable");

        VirtualWifiNetworkAgent agent = new VirtualWifiNetworkAgent(context, looper, capabilities,
                link, config, onUnwanted);
        try {
            agent.network = agent.register();
            if (agent.network == null) throw new IllegalStateException("network registration returned null");
            agent.markConnected();
        } catch (RuntimeException | Error failure) {
            try {
                agent.stop();
            } catch (Exception | Error cleanupFailure) {
                throw new WifiNetworkLifecycle.UncertainStartException(agent, failure, cleanupFailure);
            }
            throw failure;
        }
        return agent;
    }

    private static Object call(Object target, String name, Class<?>[] types, Object... args) throws Exception {
        Method method = target.getClass().getMethod(name, types);
        method.setAccessible(true);
        try {
            return method.invoke(target, args);
        } catch (InvocationTargetException exception) {
            Throwable cause = exception.getCause();
            if (cause instanceof Exception) throw (Exception) cause;
            if (cause instanceof Error) throw (Error) cause;
            throw exception;
        }
    }
}
