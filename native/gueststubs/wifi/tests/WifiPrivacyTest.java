package app.aemu.stub;

public final class WifiPrivacyTest {
    public static class Info {
        long received = -1;
        public long getApplicableRedactions() { return 7L; }
        public Object makeCopy(long flags) { received = flags; return this; }
    }
    public static class Broken { public long getApplicableRedactions() { return 7L; } }
    public static void main(String[] args) {
        Info info = new Info();
        if (WifiPrivacy.fullyRedacted(info) != info || info.received != 7L) throw new AssertionError("redactions not applied");
        WifiPrivacy.redactedFor(info, true, false, false);
        if (info.received != 6L) throw new AssertionError("fine location permission");
        WifiPrivacy.redactedFor(info, true, false, true);
        if (info.received != 2L) throw new AssertionError("network settings permission");
        WifiPrivacy.redactedFor(info, true, true, true);
        if (info.received != 0L) throw new AssertionError("all permitted");
        boolean rejected = false;
        try { WifiPrivacy.fullyRedacted(new Broken()); } catch (IllegalStateException expected) { rejected = true; }
        if (!rejected) throw new AssertionError("missing redaction method must fail closed");
        rejected = false;
        try { WifiPrivacy.fullyRedacted(null); } catch (IllegalStateException expected) { rejected = true; }
        if (!rejected) throw new AssertionError("null info");
        System.out.println("WifiPrivacyTest passed");
    }
}
