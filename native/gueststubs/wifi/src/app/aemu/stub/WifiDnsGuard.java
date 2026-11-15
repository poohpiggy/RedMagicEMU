package app.aemu.stub;
import java.util.HashMap;
import java.util.Iterator;
import java.util.Map;
import java.util.function.LongSupplier;

final class WifiDnsGuard {
    interface Source {
        State read();
        default boolean report(boolean value) { return false; }
    }
    static final class State {
        final int id; final boolean isDefault; final long generation;
        State(int id, boolean isDefault, long generation) {
            if (id < 1 || id > 65535 || generation < 0) throw new IllegalArgumentException();
            this.id = id; this.isDefault = isDefault; this.generation = generation;
        }
    }
    private static final class Pending {
        final State state; final boolean usesDefault; final long since;
        Pending(State state, boolean usesDefault, long since) { this.state = state; this.usesDefault = usesDefault; this.since = since; }
    }
    private final Source source;
    private final LongSupplier clock;
    private final Map<Long, Pending> pending = new HashMap<>();
    WifiDnsGuard(Source source) { this(source, System::nanoTime); }
    WifiDnsGuard(Source source, LongSupplier clock) {
        if (source == null || clock == null) throw new IllegalArgumentException();
        this.source = source; this.clock = clock;
    }
    synchronized boolean begin(long token, long rawId) {
        if (token <= 0 || rawId < 0 || rawId > 0xffffffffL) return false;
        long now = clock.getAsLong();
        Iterator<Pending> it = pending.values().iterator();
        while (it.hasNext()) if (now - it.next().since >= 15_000_000_000L) it.remove();
        if (pending.size() >= 32 || pending.containsKey(token)) return false;
        long id = rawId & 0x7fffffffL;
        State state = source.read();
        if (state == null || (id != state.id && !(id == 0 && state.isDefault))) return false;
        pending.put(token, new Pending(state, id == 0, now));
        return true;
    }
    synchronized boolean finish(long token) {
        Pending p = pending.remove(token);
        if (p == null || clock.getAsLong() - p.since >= 15_000_000_000L) return false;
        State now = source.read();
        return now != null && now.id == p.state.id && now.generation == p.state.generation
                && (!p.usesDefault || now.isDefault);
    }
}
