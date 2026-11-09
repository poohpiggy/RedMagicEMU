package android.os;

/** Test-only local stand-in; this class must never be included in a Guest artifact. */
public final class ServiceManager {
    public static boolean declared;
    public static IBinder current;
    public static IBinder replaceOnAdd;
    private static void checkName(String name) {
        if (!"android.hardware.vibrator.IVibratorManager/default".equals(name)) throw new AssertionError("wrong name");
    }
    public static boolean isDeclared(String name) { checkName(name); return declared; }
    public static IBinder checkService(String name) { checkName(name); return current; }
    public static void addService(String name, IBinder binder) { checkName(name); current = replaceOnAdd == null ? binder : replaceOnAdd; }
}
