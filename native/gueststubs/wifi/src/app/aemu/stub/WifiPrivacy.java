package app.aemu.stub;

/** Uses the guest framework's own redaction mask; failure never returns the original object. */
final class WifiPrivacy {
    static Object fullyRedacted(Object info) {
        return redactedFor(info, false, false, false);
    }
    static Object redactedFor(Object info, boolean location, boolean localMac, boolean networkSettings) {
        if (info == null) throw new IllegalStateException("Wi-Fi info unavailable");
        try {
            Object mask = info.getClass().getMethod("getApplicableRedactions").invoke(info);
            if (!(mask instanceof Long)) throw new IllegalStateException("redaction mask unavailable");
            long redactions = (Long) mask;
            if (location) redactions &= ~(1L << 0);
            if (localMac) redactions &= ~(1L << 1);
            if (networkSettings) redactions &= ~(1L << 2);
            Object redacted = info.getClass().getMethod("makeCopy", long.class).invoke(info, redactions);
            if (redacted == null) throw new IllegalStateException("redacted Wi-Fi info unavailable");
            return redacted;
        } catch (ReflectiveOperationException failure) {
            throw new IllegalStateException("Wi-Fi info redaction unavailable", failure);
        }
    }
    private WifiPrivacy() { }
}
