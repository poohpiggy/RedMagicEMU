package app.aemu.stub;

public final class WifiToggleAuthorizationTest {
    private static void expect(WifiToggleAuthorization.Decision expected, WifiToggleAuthorization.Facts facts) {
        WifiToggleAuthorization.Decision actual = WifiToggleAuthorization.decide(facts);
        if (actual != expected) throw new AssertionError(expected + " != " + actual);
    }

    private static WifiToggleAuthorization.Facts base() {
        WifiToggleAuthorization.Facts f = new WifiToggleAuthorization.Facts();
        f.packageMatches = true;
        f.networkSettings = false;
        f.changeWifiState = true;
        f.appOp = WifiToggleAuthorization.AppOp.ALLOWED;
        f.validCallingUser = true;
        f.privileged = false;
        f.deviceOrProfileOwner = false;
        f.systemApp = false;
        f.targetSdk = 28;
        f.guestUser = false;
        f.satelliteMode = false;
        f.airplaneMode = false;
        f.disallowChange = false;
        f.enabling = false;
        f.confirmLegacyEnable = false;
        return f;
    }

    public static void main(String[] args) {
        expect(WifiToggleAuthorization.Decision.UNAVAILABLE, null);
        WifiToggleAuthorization.Facts f = base();
        expect(WifiToggleAuthorization.Decision.ALLOWED, f);
        f.packageMatches = false;
        expect(WifiToggleAuthorization.Decision.SECURITY_EXCEPTION, f);
        f = base(); f.changeWifiState = false;
        expect(WifiToggleAuthorization.Decision.SECURITY_EXCEPTION, f);
        f = base(); f.appOp = WifiToggleAuthorization.AppOp.IGNORED;
        expect(WifiToggleAuthorization.Decision.DENIED, f);
        f = base(); f.appOp = WifiToggleAuthorization.AppOp.ERRORED;
        expect(WifiToggleAuthorization.Decision.SECURITY_EXCEPTION, f);
        f = base(); f.validCallingUser = false;
        expect(WifiToggleAuthorization.Decision.SECURITY_EXCEPTION, f);
        f = base(); f.targetSdk = 36;
        expect(WifiToggleAuthorization.Decision.DENIED, f);
        f.privileged = true;
        expect(WifiToggleAuthorization.Decision.ALLOWED, f);
        f = base(); f.networkSettings = true; f.privileged = true; f.changeWifiState = false; f.appOp = WifiToggleAuthorization.AppOp.ERRORED;
        expect(WifiToggleAuthorization.Decision.ALLOWED, f);
        f = base(); f.guestUser = true;
        expect(WifiToggleAuthorization.Decision.DENIED, f);
        f = base(); f.satelliteMode = true;
        expect(WifiToggleAuthorization.Decision.DENIED, f);
        f = base(); f.airplaneMode = true;
        expect(WifiToggleAuthorization.Decision.DENIED, f);
        f.privileged = true;
        expect(WifiToggleAuthorization.Decision.ALLOWED, f);
        f = base(); f.disallowChange = true; f.privileged = true;
        expect(WifiToggleAuthorization.Decision.DENIED, f);
        f.deviceOrProfileOwner = true;
        expect(WifiToggleAuthorization.Decision.ALLOWED, f);
        f = base(); f.enabling = true; f.confirmLegacyEnable = true;
        expect(WifiToggleAuthorization.Decision.REQUIRES_CONFIRMATION, f);
        f = base(); f.packageMatches = null;
        expect(WifiToggleAuthorization.Decision.UNAVAILABLE, f);
        f = base(); f.targetSdk = -1;
        expect(WifiToggleAuthorization.Decision.UNAVAILABLE, f);
        System.out.println("WifiToggleAuthorizationTest passed");
    }
}
