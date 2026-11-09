package app.aemu.stub;

/** Optional, bounded transaction-number counters for an isolated Radio experiment.
 * No Parcel, serial, caller identity, or request data is read or retained. */
final class RadioRequestCounter {
    static final class Snapshot {
        final int total;
        final int other;
        final boolean truncated;
        final int[] counts;
        Snapshot(int total, int other, boolean truncated, int[] counts) {
            this.total = total; this.other = other; this.truncated = truncated; this.counts = counts;
        }
    }

    private final boolean enabled;
    private final int limit;
    private final int[] counts;
    private int total;
    private int other;
    private boolean truncated;

    RadioRequestCounter(boolean enabled, int maxCode, int limit) {
        if (maxCode < 1 || maxCode > 256 || limit < 1 || limit > 4096)
            throw new IllegalArgumentException("invalid trace bounds");
        this.enabled = enabled;
        this.limit = limit;
        this.counts = new int[maxCode + 1];
    }

    synchronized void record(int code) {
        if (!enabled) return;
        if (total == limit) { truncated = true; return; }
        total++;
        if (code > 0 && code < counts.length) counts[code]++;
        else other++;
    }

    synchronized Snapshot snapshot() {
        return new Snapshot(total, other, truncated, counts.clone());
    }
}
