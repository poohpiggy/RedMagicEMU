/* RedMagicEMU, 2026-11-15: target-only GPS provider compatibility. */
package app.aemu.stub;

import android.location.Location;
import android.location.provider.IProviderRequestListener;
import android.location.provider.ProviderRequest;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.SystemClock;

import java.io.IOException;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.nio.file.Files;
import java.nio.file.NoSuchFileException;
import java.nio.file.Paths;
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
    private static final long MAX_FIX_AGE_NANOS = 30_000_000_000L;
    private static final File LOCATION_FILE = new File("/dev/aemu_location");

    private final Handler handler;
    private final Method getService;
    private final Method asInterface;
    private final Method addTestProvider;
    private final Method setTestProviderEnabled;
    private final Method setTestProviderLocation;
    private final Method addProviderRequestListener;
    private final Method removeProviderRequestListener;
    private final Object providerProperties;
    private final LocationDemand demand = new LocationDemand(Paths.get("/dev/aemu_location_demand"));

    private IBinder binder;
    private Object manager;
    private IProviderRequestListener demandListener;
    private boolean configured;
    private String lastProblem;
    private String lastDemandProblem;
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

        Class<?> locationManager = Class.forName("android.location.ILocationManager");
        addTestProvider = locationManager.getMethod(
                "addTestProvider",
                String.class,
                Class.forName("android.location.provider.ProviderProperties"),
                java.util.List.class,
                String.class,
                String.class);
        setTestProviderEnabled = locationManager.getMethod(
                "setTestProviderEnabled",
                String.class,
                boolean.class,
                String.class,
                String.class);
        setTestProviderLocation = locationManager.getMethod(
                "setTestProviderLocation",
                String.class,
                Location.class,
                String.class,
                String.class);
        addProviderRequestListener = locationManager.getMethod(
                "addProviderRequestListener", IProviderRequestListener.class);
        removeProviderRequestListener = locationManager.getMethod(
                "removeProviderRequestListener", IProviderRequestListener.class);
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
            refreshDemand();
            if (configured && demand.isRequested()) publishLocation();
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
        // Observe the framework's effective GPS request after the manager exists.
        // Enabling the test provider afterwards lets its current clients become active.
        demandListener = new IProviderRequestListener.Stub() {
            @Override
            public void onProviderRequestChanged(String provider, ProviderRequest request) {
                if (!PROVIDER.equals(provider)) return;
                handler.post(() -> {
                    if (demandListener == this) setDemand(request.isActive());
                });
            }
        };
        addProviderRequestListener.invoke(manager, demandListener);
        setTestProviderEnabled.invoke(manager, PROVIDER, true, PACKAGE, null);
        configured = true;
        System.out.println("aemu-location: enabled " + PROVIDER);
        System.out.flush();
    }

    private void reset() {
        if (demandListener != null && binder != null && binder.isBinderAlive()) {
            try {
                removeProviderRequestListener.invoke(manager, demandListener);
            } catch (ReflectiveOperationException failure) {
                problem(unwrap(failure));
            }
        }
        demandListener = null;
        setDemand(false);
        binder = null;
        manager = null;
        configured = false;
        lastLocation = null;
    }

    private void setDemand(boolean active) {
        if (!demand.setRequested(active)) return;
        System.out.println("aemu-location: GPS request " + (active ? "active" : "idle"));
        System.out.flush();
        refreshDemand();
    }

    private void refreshDemand() {
        try {
            demand.refresh(SystemClock.elapsedRealtime());
            lastDemandProblem = null;
        } catch (IOException failure) {
            String message = failure.toString();
            if (!message.equals(lastDemandProblem)) {
                lastDemandProblem = message;
                System.out.println("aemu-location: demand write failed: " + message);
                System.out.flush();
            }
        }
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
        if (text.equals(lastLocation) || text.equals("0")) return;
        Location location = parseLocation(text);
        if (location == null) return;
        setTestProviderLocation.invoke(manager, PROVIDER, location, PACKAGE, null);
        lastLocation = text;
        System.out.println("aemu-location: position synchronized");
        System.out.flush();
    }

    private static Location parseLocation(String text) {
        String[] parts = text.split("\\s+");
        if (parts.length != 7 || !parts[0].equals("1")) return null;
        try {
            double latitude = Double.parseDouble(parts[1]);
            double longitude = Double.parseDouble(parts[2]);
            Double altitude = parts[3].equals("null") ? null : Double.valueOf(parts[3]);
            float accuracy = Float.parseFloat(parts[4]);
            long elapsedNanos = Long.parseLong(parts[5]);
            long timeMillis = Long.parseLong(parts[6]);
            long nowNanos = SystemClock.elapsedRealtimeNanos();
            if (!Double.isFinite(latitude) || latitude < -90 || latitude > 90
                    || !Double.isFinite(longitude) || longitude < -180 || longitude > 180
                    || (altitude != null && !Double.isFinite(altitude))
                    || !Float.isFinite(accuracy) || accuracy < 0
                    || elapsedNanos <= 0 || elapsedNanos > nowNanos
                    || nowNanos - elapsedNanos > MAX_FIX_AGE_NANOS || timeMillis <= 0) return null;

            Location location = new Location(PROVIDER);
            location.setLatitude(latitude);
            location.setLongitude(longitude);
            if (altitude != null) location.setAltitude(altitude);
            location.setAccuracy(accuracy);
            location.setElapsedRealtimeNanos(elapsedNanos);
            location.setTime(timeMillis);
            return location;
        } catch (NumberFormatException malformed) {
            return null;
        }
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
