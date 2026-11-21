package cn.nubia.camera.adapter.system;

import android.util.Log;
import java.util.HashMap;
import java.util.Map;

/** Nubia's optional single-camera adapter, supplied only inside the NX809J Guest. */
public final class AdapterSingleZteRedMagic11 {
    private static final String STOCK_ADAPTER = "cn.nubia.camera.adapter.AdapterZteRedMagic11";
    private static final String MODE_SWITCHER = "ModeSwitcherSensor";

    public void overwriteFeatures(Map<String, String> features) throws ReflectiveOperationException {
        ClassLoader loader = Thread.currentThread().getContextClassLoader();
        Class<?> stockAdapter = Class.forName(STOCK_ADAPTER, true, loader);
        Map<String, String> updated = new HashMap<>(features);
        stockAdapter.getMethod("overwriteFeatures", Map.class)
                .invoke(stockAdapter.getConstructor().newInstance(), updated);
        String modes = updated.get(MODE_SWITCHER);
        if (modes == null || modes.isEmpty()) {
            updated.put(MODE_SWITCHER, "51.main");
        } else {
            for (String mode : modes.split("/")) {
                if (mode.startsWith("51.")) {
                    features.putAll(updated);
                    return;
                }
            }
            updated.put(MODE_SWITCHER, modes + "/51.main");
        }
        features.putAll(updated);
        Log.i("RmeProVideoAdapter", "Configured the primary camera for professional video");
    }
}
