package app.aemu.stub;

public final class VibrationControllerTest {
    private static final class Sink implements VibrationController.Sink {
        int last = -1;
        public void play(int durationMs) { last = durationMs; }
        public void cancel() { last = 0; }
    }
    private static void check(boolean b) { if (!b) throw new AssertionError(); }
    private static void expect(Class<? extends Throwable> type, RunnableWithException operation) throws Exception {
        try { operation.run(); } catch (Throwable t) { if (type.isInstance(t)) return; throw t; }
        throw new AssertionError("expected " + type.getName());
    }
    private interface RunnableWithException { void run() throws Exception; }
    public static void main(String[] args) throws Exception {
        Sink sink = new Sink();
        VibrationController c = new VibrationController(sink);
        expect(SecurityException.class, () -> c.on(10001, 200));
        check(sink.last == -1);
        expect(SecurityException.class, () -> c.off(10001));
        expect(SecurityException.class, () -> c.on(1000, 0));
        expect(SecurityException.class, () -> c.on(1000, 5001));
        check(sink.last == -1);
        expect(SecurityException.class, () -> c.on(1000, 300));
        expect(SecurityException.class, () -> c.off(1000));
        check(sink.last == -1);
        c.close();
        check(sink.last == -1);
        expect(SecurityException.class, () -> c.on(1000, 300));
        System.out.println("VibrationControllerTest passed");
    }
}
