package app.aemu.stub;

/**
 * Experimental decision model for Android 16 setWifiEnabled(String, boolean).
 * The caller must obtain every fact from the guest's Binder caller and Android
 * services, not from data asserted by the calling app. This class is not wired
 * to the service and makes no claim that facts can currently be obtained.
 *
 * Based on the behavior described by Android's WifiServiceImpl on the
 * android16-release branch, enforceChangePermission and setWifiEnabled:
 * https://android.googlesource.com/platform/packages/modules/Wifi/+/refs/heads/android16-release/service/java/com/android/server/wifi/WifiServiceImpl.java
 */
final class WifiToggleAuthorization {
    enum Decision { ALLOWED, DENIED, SECURITY_EXCEPTION, REQUIRES_CONFIRMATION, UNAVAILABLE }
    enum AppOp { ALLOWED, IGNORED, ERRORED }

    static final class Facts {
        Boolean packageMatches, networkSettings, changeWifiState;
        AppOp appOp;
        Boolean validCallingUser, privileged, deviceOrProfileOwner, systemApp;
        Integer targetSdk;
        Boolean guestUser, satelliteMode, airplaneMode, disallowChange;
        Boolean enabling, confirmLegacyEnable;
    }

    static Decision decide(Facts f) {
        if (f == null || f.packageMatches == null || f.networkSettings == null
                || f.changeWifiState == null || f.appOp == null || f.validCallingUser == null
                || f.privileged == null || f.deviceOrProfileOwner == null || f.systemApp == null
                || f.targetSdk == null || f.targetSdk < 0 || f.guestUser == null
                || f.satelliteMode == null || f.airplaneMode == null || f.disallowChange == null
                || f.enabling == null || f.confirmLegacyEnable == null
                || (f.networkSettings && !f.privileged)) return Decision.UNAVAILABLE;
        if (!f.packageMatches) return Decision.SECURITY_EXCEPTION;
        if (!f.networkSettings) {
            if (!f.changeWifiState) return Decision.SECURITY_EXCEPTION;
            if (f.appOp == AppOp.ERRORED) return Decision.SECURITY_EXCEPTION;
            if (f.appOp != AppOp.ALLOWED) return Decision.DENIED;
        }
        if (!f.validCallingUser) return Decision.SECURITY_EXCEPTION;
        boolean thirdParty = !f.privileged && !f.deviceOrProfileOwner && !f.systemApp;
        boolean legacy = f.targetSdk < 29 && !f.guestUser;
        if (thirdParty && !legacy) return Decision.DENIED;
        if (f.satelliteMode || (f.airplaneMode && !f.privileged)
                || (f.disallowChange && !f.deviceOrProfileOwner)) return Decision.DENIED;
        if (f.enabling && legacy && thirdParty && f.confirmLegacyEnable) return Decision.REQUIRES_CONFIRMATION;
        return Decision.ALLOWED;
    }

    private WifiToggleAuthorization() { }
}
