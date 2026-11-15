/* RedMagicEMU, 2026-11-15: target-only GPS provider compatibility. */
package app.aemu.stub;

import android.location.Location;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;

import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.nio.file.Files;
import java.nio.file.NoSuchFileException;
import java.nio.charset.StandardCharsets;
import java.util.Collections;
import java.io.File;

/**
 * The target's LocationManagerService only publishes its network provider. The
 * stock compass nevertheless requires the name "gps" and crashes when it is
 * absent. Register a test provider as root so requests can bind; do not invent a
 * location sample.
 *
 * The service follows system_server replacements and is intentionally limited
 * to the API 36 launch path in GuestVm.
 */
public final class LocationStub {
    private static final String SERVICE = "location";
    private static final String PROVIDER = "gps";
    private static final String PACKAGE = "android";
    private static final long RETRY_MS = 1000;
    private static final long CHECK_MS = 1000;
    private static final File LOCATION_FILE = new File("/dev/aemu_location");

    private final Handler handler;
    private final Method getService;
    private final Method asInterface;
    private final Method addTestProvider;
    private final Method setTestProviderEnabled;
    private final Method setTestProviderLocation;
    private final Object providerProperties;

    private IBinder binder;
    private Object manager;
    private boolean configured;
    private boolean providerEnabled;
    private String lastProblem;
    private String lastLocation;

    private LocationStub() throws Exception {
        handler = new Handler(Looper.myLooper());
        getService = Class.forName("android.os.ServiceManager").getMethod("getService", String.class);
        asInterface = Class.forName("android.location.ILocationManager$Stub")
                .getMethod("asInterface", IBinder.class);

        Class<?> builderClass = Class.forName("android.location.provider.ProviderProperties$Builder");
        Object builder = builderClass.getConstructor().newInstance();
        builderClass.getMethod("setHasAltitudeSupport", boolean.class).invoke(builder, true);
        builderClass.getMethod("setHasBearingSupport", boolean.class).invoke(builder, true);
        builderClass.getMethod("setHasSpeedSupport", boolean.class).invoke(builder, true);
        builderClass.getMethod("setHasSatelliteRequirement", boolean.class).invoke(builder, true);
        providerProperties = builderClass.getMethod("build").invoke(builder);

        addTestProvider = Class.forName("android.location.ILocationManager").getMethod(
                "addTestProvider",
                String.class,
                Class.forName("android.location.provider.ProviderProperties"),
                java.util.List.class,
                String.class,
                String.class);
        setTestProviderEnabled = Class.forName("android.location.ILocationManager").getMethod(
                "setTestProviderEnabled",
                String.class,
                boolean.class,
                String.class,
                String.class);
        setTestProviderLocation = Class.forName("android.location.ILocationManager").getMethod(
                "setTestProviderLocation",
                String.class,
                Location.class,
                String.class,
                String.class);
    }

    private void poll() {
        try {
            if (binder != null && !binder.isBinderAlive()) reset();
            if (binder == null) {
                IBinder found = (IBinder) getService.invoke(null, SERVICE);
                if (found != null) {
                    binder = found;
                    manager = asInterface.invoke(null, found);
                    configured = false;
                }
            }
            if (manager != null && !configured) configure();
            if (manager != null && configured) publishLocation();
            problem(null);
        } catch (Throwable failure) {
            reset();
            problem(unwrap(failure));
        }
        handler.postDelayed(this::poll, binder == null ? RETRY_MS : CHECK_MS);
    }

    private void configure() throws Exception {
        try {
            addTestProvider.invoke(manager, PROVIDER, providerProperties,
                    Collections.emptyList(), PACKAGE, null);
            System.out.println("aemu-location: registered " + PROVIDER);
        } catch (InvocationTargetException failure) {
            String message = String.valueOf(unwrap(failure));
            if (!message.contains("already exists")) throw failure;
        }
        setTestProviderEnabled.invoke(manager, PROVIDER, true, PACKAGE, null);
        providerEnabled = true;
        configured = true;
        System.out.println("aemu-location: enabled " + PROVIDER);
        System.out.flush();
    }

    private void reset() {
        binder = null;
        manager = null;
        configured = false;
        providerEnabled = false;
        lastLocation = null;
    }

    private void publishLocation() throws Exception {
        final String text;
        try {
            byte[] bytes = Files.readAllBytes(LOCATION_FILE.toPath());
            if (bytes.length == 0 || bytes.length > 512) return;
            text = new String(bytes, StandardCharsets.US_ASCII).trim();
        } catch (NoSuchFileException missing) {
            return;
        }
        if (text.equals(lastLocation)) return;
        if (text.equals("0")) {
            if (providerEnabled) {
                setTestProviderEnabled.invoke(manager, PROVIDER, false, PACKAGE, null);
                providerEnabled = false;
                System.out.println("aemu-location: disabled " + PROVIDER);
                System.out.flush();
            }
            lastLocation = text;
            return;
        }
        String[] parts = text.split("\\s+");
        if (parts.length != 7 || !parts[0].equals("1")) return;
        Location location = new Location(PROVIDER);
        location.setLatitude(Double.parseDouble(parts[1]));
        location.setLongitude(Double.parseDouble(parts[2]));
        if (!parts[3].equals("null")) location.setAltitude(Double.parseDouble(parts[3]));
        location.setAccuracy(Float.parseFloat(parts[4]));
        location.setElapsedRealtimeNanos(Long.parseLong(parts[5]));
        location.setTime(Long.parseLong(parts[6]));
        if (!providerEnabled) {
            setTestProviderEnabled.invoke(manager, PROVIDER, true, PACKAGE, null);
            providerEnabled = true;
        }
        setTestProviderLocation.invoke(manager, PROVIDER, location, PACKAGE, null);
        lastLocation = text;
        System.out.println("aemu-location: position synchronized");
        System.out.flush();
    }

    private void problem(Throwable failure) {
        String message = failure == null ? null : failure.toString();
        if (message == null ? lastProblem == null : message.equals(lastProblem)) return;
        lastProblem = message;
        if (message != null) {
            System.out.println("aemu-location: " + message);
            System.out.flush();
        }
    }

    private static Throwable unwrap(Throwable failure) {
        return failure instanceof InvocationTargetException &&
                ((InvocationTargetException) failure).getCause() != null
                ? ((InvocationTargetException) failure).getCause() : failure;
    }

    public static void main(String[] args) throws Exception {
        Looper.prepare();
        LocationStub stub = new LocationStub();
        stub.poll();
        Looper.loop();
    }
}
