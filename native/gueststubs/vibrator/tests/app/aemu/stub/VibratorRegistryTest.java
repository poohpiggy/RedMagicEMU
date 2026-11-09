package app.aemu.stub;

import android.os.IBinder;
import java.lang.reflect.Constructor;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/** Host-only registration contract; never publishes an Android service. */
public final class VibratorRegistryTest {
    private static final String NAME = "android.hardware.vibrator.IVibratorManager/default";
    private static void check(boolean ok, String message) { if (!ok) throw new AssertionError(message); }

    private static final class Fake {
        final List<String> calls = new ArrayList<>();
        final IBinder binder;
        boolean declared = true, occupied, published = true, failAdd;
        Fake(IBinder binder) { this.binder = binder; }
        Object backend(Class<?> type) {
            return Proxy.newProxyInstance(type.getClassLoader(), new Class<?>[]{type}, (proxy, method, args) -> {
                check(NAME.equals(args[0]), "unexpected name");
                calls.add(method.getName());
                switch (method.getName()) {
                    case "isDeclared": return declared;
                    case "isOccupied": return occupied;
                    case "add":
                        check(args[1] == binder, "wrong binder");
                        if (failAdd) throw new IllegalStateException("add failed");
                        return null;
                    case "isObservable": check(args[1] == binder, "wrong observed binder"); return published;
                    default: throw new AssertionError("unexpected backend call");
                }
            });
        }
    }
    private static Throwable fail(Method method, Object target) throws Exception {
        try { method.invoke(target); throw new AssertionError("registration unexpectedly succeeded"); }
        catch (InvocationTargetException expected) { return expected.getCause(); }
    }
    public static void main(String[] args) throws Exception {
        Class<?> registry;
        try { registry = Class.forName("app.aemu.stub.VibratorRegistry"); }
        catch (ClassNotFoundException e) { throw new AssertionError("vibrator registry missing", e); }
        Class<?> backend = Class.forName("app.aemu.stub.VibratorRegistry$Backend");
        Constructor<?> constructor = registry.getDeclaredConstructor(backend, IBinder.class);
        constructor.setAccessible(true);
        Method start = registry.getDeclaredMethod("start"); start.setAccessible(true);
        Method exit = registry.getDeclaredMethod("mustExitProcess"); exit.setAccessible(true);
        IBinder binder = (IBinder) Proxy.newProxyInstance(IBinder.class.getClassLoader(),
                new Class<?>[]{IBinder.class}, (p, m, a) -> { throw new AssertionError("unexpected binder call"); });
        Fake good = new Fake(binder);
        Object instance = constructor.newInstance(good.backend(backend), binder);
        check("OBSERVED_UNVERIFIED".equals(String.valueOf(start.invoke(instance))), "observation misrepresented as ownership");
        check(good.calls.equals(Arrays.asList("isDeclared", "isOccupied", "add", "isObservable")), "wrong registration sequence");
        check((Boolean) exit.invoke(instance), "published process must retire");
        check(fail(start, instance) instanceof IllegalStateException, "second start accepted");
        for (int kind = 0; kind < 4; kind++) {
            Fake bad = new Fake(binder);
            if (kind == 0) bad.declared = false;
            if (kind == 1) bad.occupied = true;
            if (kind == 2) bad.failAdd = true;
            if (kind == 3) bad.published = false;
            Object failed = constructor.newInstance(bad.backend(backend), binder);
            check(fail(start, failed) instanceof IllegalStateException, "failure not propagated");
            check((Boolean) exit.invoke(failed), "failed start must retire");
            if (kind < 2) check(!bad.calls.contains("add"), "published after failed preflight");
        }
        Method platform = registry.getDeclaredMethod("registerWithServiceManager", IBinder.class);
        platform.setAccessible(true);
        IBinder live = (IBinder) Proxy.newProxyInstance(IBinder.class.getClassLoader(),
                new Class<?>[]{IBinder.class}, (p, m, a) -> {
                    if (m.getName().equals("isBinderAlive")) return true;
                    if (m.getName().equals("getInterfaceDescriptor")) return "android.hardware.vibrator.IVibratorManager";
                    throw new AssertionError("unexpected binder call: " + m.getName());
                });
        android.os.ServiceManager.declared = true;
        android.os.ServiceManager.current = null;
        Object registered = platform.invoke(null, live);
        check(android.os.ServiceManager.current == live, "platform entry did not publish binder");
        Method observation = registry.getDeclaredMethod("observation"); observation.setAccessible(true);
        check("OBSERVED_UNVERIFIED".equals(String.valueOf(observation.invoke(registered))), "platform result claims ownership");
        try { platform.invoke(null, live); throw new AssertionError("occupied platform name accepted"); }
        catch (InvocationTargetException expected) { check(expected.getCause() instanceof IllegalStateException, "wrong occupied error"); }
        android.os.ServiceManager.current = null;
        android.os.ServiceManager.declared = false;
        try { platform.invoke(null, live); throw new AssertionError("undeclared platform name accepted"); }
        catch (InvocationTargetException expected) { check(expected.getCause() instanceof IllegalStateException, "wrong undeclared error"); }
        check(android.os.ServiceManager.current == null, "undeclared service was published");
        android.os.ServiceManager.declared = true;
        IBinder dead = (IBinder) Proxy.newProxyInstance(IBinder.class.getClassLoader(),
                new Class<?>[]{IBinder.class}, (p, m, a) -> {
                    if (m.getName().equals("isBinderAlive")) return false;
                    throw new AssertionError("unexpected binder call: " + m.getName());
                });
        try { platform.invoke(null, dead); throw new AssertionError("dead service accepted"); }
        catch (InvocationTargetException expected) { check(expected.getCause() instanceof IllegalStateException, "wrong dead error"); }
        IBinder impostor = (IBinder) Proxy.newProxyInstance(IBinder.class.getClassLoader(),
                new Class<?>[]{IBinder.class}, (p, m, a) -> {
                    if (m.getName().equals("isBinderAlive")) return true;
                    if (m.getName().equals("getInterfaceDescriptor")) return "android.hardware.vibrator.IVibratorManager";
                    throw new AssertionError("unexpected binder call: " + m.getName());
                });
        android.os.ServiceManager.current = null;
        android.os.ServiceManager.replaceOnAdd = impostor;
        Object collision = platform.invoke(null, live);
        check(android.os.ServiceManager.current == impostor, "collision not simulated");
        check("OBSERVED_UNVERIFIED".equals(String.valueOf(observation.invoke(collision))),
                "matching descriptor falsely proves ownership");
        System.out.println("VIBRATOR_REGISTRY_CONTRACT_OK");
    }
}
