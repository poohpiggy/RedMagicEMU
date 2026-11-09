package app.aemu.stub;

import android.os.IBinder;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.util.concurrent.TimeUnit;

/** Guest-only, synthetic names and synthetic state; never used by the VM launcher. */
public final class RadioSyntheticProcessTest {
    private static final String[] INTERFACES = {
        "android.hardware.radio.config.IRadioConfig", "android.hardware.radio.modem.IRadioModem",
        "android.hardware.radio.sim.IRadioSim", "android.hardware.radio.network.IRadioNetwork",
        "android.hardware.radio.data.IRadioData", "android.hardware.radio.messaging.IRadioMessaging",
        "android.hardware.radio.voice.IRadioVoice"
    };
    private static Object manager(String method, Class<?>[] types, Object... args) throws Exception {
        return Class.forName("android.os.ServiceManager").getMethod(method, types).invoke(null, args);
    }
    private static IBinder lookup(String name) throws Exception {
        return (IBinder) manager("checkService", new Class<?>[]{String.class}, name);
    }
    private static void check(boolean value, String message) { if (!value) throw new AssertionError(message); }
    private static String name(String token, int i) {
        return "app.redmagicemu.test.radio." + token + "." + RadioSyntheticRegistry.SUFFIXES[i];
    }
    private static void server(String token) throws Exception {
        SyntheticSingleSlot model = SyntheticSingleSlot.forTests();
        RadioModemService modem = new RadioModemService("slot1", model);
        RadioVoiceService voice = new RadioVoiceService("slot1", 64, 5000, model);
        IBinder[] binders = {new RadioConfigService("slot1", model).asBinder(), modem.asBinder(),
            new RadioSimService("slot1", model).asBinder(), new RadioNetworkService("slot1", model).asBinder(),
            new RadioDataService("slot1", model).asBinder(), new RadioMessagingService("slot1", model).asBinder(),
            voice.asBinder()};
        for (IBinder binder : binders) {
            Field modelField = null;
            for (Field f : binder.getClass().getDeclaredFields())
                if (f.getType() == SyntheticSingleSlot.class) { modelField = f; break; }
            check(modelField != null, "missing state model");
            modelField.setAccessible(true);
            check(modelField.get(binder) == model, "different state model");
        }
        check(model.isSimReady() && !model.isNetworkRegistered() && !model.canDeliverVoiceOrSms(), "unexpected powered-off model state");
        RadioSyntheticRegistry.Backend backend = new RadioSyntheticRegistry.Backend() {
            public boolean exists(String n) throws Exception { return lookup(n) != null; }
            public void add(String n, IBinder b) throws Exception {
                manager("addService", new Class<?>[]{String.class, IBinder.class}, n, b);
            }
            public boolean ready(String n, IBinder expected) throws Exception {
                IBinder actual = lookup(n);
                return actual != null && actual.isBinderAlive() &&
                    actual.getInterfaceDescriptor().equals(expected.getInterfaceDescriptor());
            }
        };
        try (RadioSyntheticRegistry registry = new RadioSyntheticRegistry(backend, token, binders, 5000)) {
            registry.start();
            check(registry.isReady(), "registry not ready");
            System.out.println("RADIO_SYNTHETIC_SERVER_READY"); System.out.flush();
            TimeUnit.SECONDS.sleep(35);
        } finally { voice.close(); modem.detachStateListener(); model.disconnect(); }
        System.out.println("RADIO_SYNTHETIC_SERVER_EXIT"); System.out.flush();
    }
    private static void client(String token) throws Exception {
        for (int i = 0; i < 7; i++) {
            Class<?> iface = Class.forName(INTERFACES[i]);
            IBinder binder = lookup(name(token, i));
            check(binder != null && binder.isBinderAlive(), "service absent");
            String descriptor = (String) iface.getField("DESCRIPTOR").get(null);
            check(descriptor.equals(binder.getInterfaceDescriptor()), "descriptor mismatch");
            check(binder.queryLocalInterface(descriptor) == null, "not cross-process");
            Class<?> stub = Class.forName(INTERFACES[i] + "$Stub");
            Object proxy = stub.getMethod("asInterface", IBinder.class).invoke(null, binder);
            Method version = iface.getMethod("getInterfaceVersion");
            Method hash = iface.getMethod("getInterfaceHash");
            check(version.invoke(proxy).equals(iface.getField("VERSION").get(null)), "version mismatch");
            check(hash.invoke(proxy).equals(iface.getField("HASH").get(null)), "hash mismatch");
        }
        System.out.println("RADIO_SYNTHETIC_CLIENT_OK"); System.out.flush();
    }
    public static void main(String[] args) throws Exception {
        if (args.length != 2 || !args[1].matches("[0-9a-f]{32}")) throw new IllegalArgumentException("mode/token");
        try {
            if (args[0].equals("server")) server(args[1]);
            else if (args[0].equals("client")) client(args[1]);
            else throw new IllegalArgumentException("mode");
            System.exit(0);
        } catch (Throwable failure) {
            System.err.println("RADIO_SYNTHETIC_PROCESS_FAILED " + failure.getClass().getSimpleName()
                + " phase=" + failure.getMessage());
            Throwable cause = failure.getCause();
            while (cause != null) { System.err.println("RADIO_SYNTHETIC_CAUSE " + cause.getClass().getName() + " " + cause.getMessage()); cause = cause.getCause(); }
            System.exit(1);
        }
    }
}
