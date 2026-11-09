package app.aemu.stub;

import android.hardware.radio.config.IRadioConfig;
import android.os.IBinder;
import java.util.concurrent.TimeUnit;

/** Guest-only canary. Staging VINTF and ensuring process lifetime are external prerequisites. */
public final class RadioVintfCanaryTest {
    private static Object manager(String method, Class<?>[] types, Object... args) throws Exception {
        return Class.forName("android.os.ServiceManager").getMethod(method, types).invoke(null, args);
    }
    private static IBinder checkService(String name) throws Exception {
        return (IBinder) manager("checkService", new Class<?>[]{String.class}, name);
    }
    private static boolean isDeclared(String name) throws Exception {
        return (Boolean) manager("isDeclared", new Class<?>[]{String.class}, name);
    }
    private static void check(boolean ok, String message) { if (!ok) throw new AssertionError(message); }
    private static void server(String token) throws Exception {
        SyntheticSingleSlot state = SyntheticSingleSlot.forTests();
        check(state.isSimReady() && !state.isNetworkRegistered() && !state.canDeliverVoiceOrSms(), "unexpected powered-off model state");
        IRadioConfig.Stub config = new RadioConfigService("slot1", state);
        RadioCanaryRegistration.Backend backend = new RadioCanaryRegistration.Backend() {
            public boolean declared(String name) throws Exception { return isDeclared(name); }
            public boolean exists(String name) throws Exception { return checkService(name) != null; }
            public void add(String name, IBinder binder) throws Exception {
                manager("addService", new Class<?>[]{String.class, IBinder.class}, name, binder);
            }
            public boolean ready(String name, IBinder expected) throws Exception {
                IBinder actual = checkService(name);
                return actual != null && actual.isBinderAlive() && IRadioConfig.DESCRIPTOR.equals(actual.getInterfaceDescriptor());
            }
        };
        try (RadioCanaryRegistration registration = new RadioCanaryRegistration(backend, token, config.asBinder(), 5000)) {
            registration.start(); check(registration.isReady(), "registration not ready");
            System.out.println("RADIO_CANARY_SERVER_READY"); System.out.flush();
            TimeUnit.SECONDS.sleep(35);
        } finally { state.disconnect(); }
        System.out.println("RADIO_CANARY_SERVER_EXIT"); System.out.flush();
    }
    private static void client(String token) throws Exception {
        String name = RadioCanaryRegistration.PREFIX + token;
        check(isDeclared(name), "not declared");
        IBinder binder = checkService(name);
        check(binder != null && binder.isBinderAlive(), "binder absent");
        check(binder.queryLocalInterface(IRadioConfig.DESCRIPTOR) == null, "not remote");
        check(IRadioConfig.DESCRIPTOR.equals(binder.getInterfaceDescriptor()), "descriptor mismatch");
        IRadioConfig config = IRadioConfig.Stub.asInterface(binder);
        check(config.getInterfaceVersion() == IRadioConfig.VERSION && IRadioConfig.HASH.equals(config.getInterfaceHash()), "version/hash mismatch");
        System.out.println("RADIO_CANARY_CLIENT_OK"); System.out.flush();
    }
    public static void main(String[] args) throws Exception {
        if (args.length != 2 || !args[1].matches("[0-9a-f]{32}")) throw new IllegalArgumentException("mode/token");
        try {
            String name = RadioCanaryRegistration.PREFIX + args[1];
            if (args[0].equals("server")) server(args[1]);
            else if (args[0].equals("client")) client(args[1]);
            else if (args[0].equals("absent")) {
                check(checkService(name) == null, "binder still present");
                System.out.println("RADIO_CANARY_ABSENT"); System.out.flush();
            } else if (args[0].equals("declared")) {
                System.out.println(isDeclared(name) ? "RADIO_CANARY_DECLARED" : "RADIO_CANARY_UNDECLARED"); System.out.flush();
            } else throw new IllegalArgumentException("mode");
            System.exit(0);
        } catch (Throwable failure) {
            System.err.println("RADIO_CANARY_FAILED " + failure.getClass().getSimpleName() + " phase=" + failure.getMessage());
            Throwable cause = failure.getCause();
            while (cause != null) { System.err.println("RADIO_CANARY_CAUSE " + cause.getClass().getName() + " " + cause.getMessage()); cause = cause.getCause(); }
            System.exit(1);
        }
    }
}
