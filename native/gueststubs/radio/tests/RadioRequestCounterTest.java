package app.aemu.stub;

/** Isolated counter contract; it records only bounded transaction numbers. */
public final class RadioRequestCounterTest {
    private static void check(boolean ok, String message) {
        if (!ok) throw new AssertionError(message);
    }

    public static void main(String[] args) throws Exception {
        RadioRequestCounter disabled = new RadioRequestCounter(false, 4, 2);
        disabled.record(1);
        check(disabled.snapshot().total == 0, "disabled trace recorded a request");

        RadioRequestCounter trace = new RadioRequestCounter(true, 4, 3);
        trace.record(1); trace.record(2); trace.record(1); trace.record(3);
        RadioRequestCounter.Snapshot snapshot = trace.snapshot();
        check(snapshot.total == 3 && snapshot.truncated, "missing trace limit");
        check(snapshot.counts[1] == 2 && snapshot.counts[2] == 1 && snapshot.counts[3] == 0,
                "wrong per-code count");
        snapshot.counts[1] = 0;
        check(trace.snapshot().counts[1] == 2, "snapshot changed underlying counter");

        RadioRequestCounter bounded = new RadioRequestCounter(true, 2, 2);
        bounded.record(-1); bounded.record(Integer.MAX_VALUE); bounded.record(1);
        snapshot = bounded.snapshot();
        check(snapshot.total == 2 && snapshot.other == 2 && snapshot.truncated,
                "out-of-range codes or cap handled incorrectly");

        RadioRequestCounter concurrent = new RadioRequestCounter(true, 1, 1000);
        Thread[] threads = new Thread[8];
        for (int i = 0; i < threads.length; i++) {
            threads[i] = new Thread(() -> { for (int j = 0; j < 500; j++) concurrent.record(1); });
            threads[i].start();
        }
        for (Thread thread : threads) thread.join();
        snapshot = concurrent.snapshot();
        check(snapshot.total == 1000 && snapshot.counts[1] == 1000 && snapshot.truncated,
                "concurrent writes exceeded limit or were lost");
        System.out.println("RADIO_REQUEST_COUNTER_OK");
    }
}
