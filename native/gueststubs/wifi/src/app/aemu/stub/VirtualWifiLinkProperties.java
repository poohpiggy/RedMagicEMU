package app.aemu.stub;

import android.net.LinkProperties;
import java.lang.reflect.Constructor;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.net.InetAddress;
import java.util.List;

/** Describes the guest's existing interface and route, without changing either. */
final class VirtualWifiLinkProperties {
    private VirtualWifiLinkProperties() { }

    static LinkProperties create(String iface, InetAddress address, int prefix, InetAddress gateway,
            List<InetAddress> dns) throws Exception {
        if (iface == null || iface.isEmpty() || address == null || gateway == null || dns == null
                || prefix < 1 || prefix > address.getAddress().length * 8
                || address.getAddress().length != gateway.getAddress().length) {
            throw new IllegalArgumentException("invalid existing network configuration");
        }
        for (InetAddress server : dns) {
            if (server == null) throw new IllegalArgumentException("DNS server is null");
        }

        LinkProperties link = new LinkProperties();
        link.setInterfaceName(iface);
        Class<?> linkAddressClass = Class.forName("android.net.LinkAddress");
        Object linkAddress = linkAddressClass.getConstructor(InetAddress.class, int.class)
                .newInstance(address, prefix);
        requireAdded(invoke(link, "addLinkAddress", new Class<?>[] { linkAddressClass }, linkAddress));

        Class<?> prefixClass = Class.forName("android.net.IpPrefix");
        Object subnet = prefixClass.getConstructor(InetAddress.class, int.class).newInstance(address, prefix);
        Class<?> routeClass = Class.forName("android.net.RouteInfo");
        Constructor<?> route = routeClass.getConstructor(prefixClass, InetAddress.class, String.class, int.class);
        Object connected = route.newInstance(subnet, null, iface, 1 /* RTN_UNICAST */);
        Object defaultRoute = route.newInstance(null, gateway, iface, 1 /* RTN_UNICAST */);
        requireAdded(invoke(link, "addRoute", new Class<?>[] { routeClass }, connected));
        requireAdded(invoke(link, "addRoute", new Class<?>[] { routeClass }, defaultRoute));
        for (InetAddress server : dns) {
            invoke(link, "addDnsServer", new Class<?>[] { InetAddress.class }, server);
        }
        return link;
    }

    private static void requireAdded(Object result) {
        if (!Boolean.TRUE.equals(result)) throw new IllegalStateException("link entry was rejected");
    }

    private static Object invoke(Object target, String name, Class<?>[] types, Object... args) throws Exception {
        Method method = target.getClass().getMethod(name, types);
        try {
            return method.invoke(target, args);
        } catch (InvocationTargetException failure) {
            Throwable cause = failure.getCause();
            if (cause instanceof Exception) throw (Exception) cause;
            if (cause instanceof Error) throw (Error) cause;
            throw failure;
        }
    }
}
