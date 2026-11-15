package app.aemu.stub;

/** Detects Guest airplane-mode changes without changing host radios. */
final class WifiAirplaneModeObserver {
    interface CurrentState { boolean enabled(); }
    interface Toggle { boolean setEnabled(boolean enabled); }

    private final CurrentState currentState;
    private final Toggle toggle;
    private Boolean lastAirplaneMode;
    private boolean restoreEnabled;
    private Boolean pending;

    WifiAirplaneModeObserver(CurrentState currentState, Toggle toggle) {
        if (currentState == null || toggle == null) throw new IllegalArgumentException("Wi-Fi callbacks required");
        this.currentState = currentState;
        this.toggle = toggle;
    }

    void observe(boolean airplaneMode) {
        if (lastAirplaneMode == null) {
            // Preserve a manual Wi-Fi choice made before this process started.
            lastAirplaneMode = airplaneMode;
            return;
        }
        if (lastAirplaneMode != airplaneMode) {
            lastAirplaneMode = airplaneMode;
            if (airplaneMode) {
                restoreEnabled = currentState.enabled();
                pending = restoreEnabled ? Boolean.FALSE : null;
            } else {
                pending = restoreEnabled && !currentState.enabled() ? Boolean.TRUE : null;
                restoreEnabled = false;
            }
        }
        if (pending != null && toggle.setEnabled(pending.booleanValue())) pending = null;
    }
}
