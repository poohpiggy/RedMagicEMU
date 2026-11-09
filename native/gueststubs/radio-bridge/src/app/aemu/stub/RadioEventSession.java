package app.aemu.stub;

import java.util.Arrays;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.locks.ReentrantLock;

/** Isolated, single-session RME1 dispatcher. ACK is local submission status only. */
public final class RadioEventSession {
    private static final int HEADER_SIZE = 23;
    private static final int MAX_PAYLOAD = 256;
    private final long epoch;
    private final RadioEventEndpoint endpoint;
    private final RadioEventSessionManager owner;
    private final ReentrantLock receiveLock = new ReentrantLock();
    private final AtomicBoolean closeRequested = new AtomicBoolean();
    private volatile CloseResult closeResult;
    private volatile Throwable closeFailure;
    private long lastSequence;
    private boolean dispatching;

    /** Unmanaged constructor retained for isolated legacy tests only. */
    RadioEventSession(long epoch, RadioEventEndpoint endpoint) { this(epoch, endpoint, null); }

    RadioEventSession(long epoch, RadioEventEndpoint endpoint, RadioEventSessionManager owner) {
        if (epoch == 0 || endpoint == null) throw new IllegalArgumentException("epoch and endpoint required");
        if (owner == null && !endpoint.allowsUnmanagedSession()) throw new IllegalStateException("unmanaged session is retired");
        this.epoch = epoch;
        this.endpoint = endpoint;
        this.owner = owner;
    }

    enum CloseResult { CLEANED, IN_PROGRESS, UNCERTAIN }
    long epoch() { return epoch; }
    RadioEventSessionManager owner() { return owner; }
    Throwable closeFailure() { return closeFailure; }

    /** A busy or failed close retires this session and requires its process to exit. */
    CloseResult closeSession() {
        if (!closeRequested.compareAndSet(false, true)) {
            CloseResult result = closeResult;
            return result == null ? CloseResult.IN_PROGRESS : result;
        }
        boolean acquired = receiveLock.tryLock();
        boolean cleaned = false;
        try {
            if (!acquired || dispatching) {
                closeFailure = new IllegalStateException("event still in flight; process exit required");
            } else {
                try {
                    endpoint.closeEventSession(this);
                    cleaned = true;
                } catch (android.os.RemoteException | RuntimeException error) {
                    closeFailure = error;
                } catch (Error fatal) {
                    closeFailure = fatal;
                    throw fatal;
                }
            }
        } finally {
            if (acquired) receiveLock.unlock();
            closeResult = cleaned ? CloseResult.CLEANED : CloseResult.UNCERTAIN;
            if (owner != null && owner.isActive(this)) owner.finished(this, cleaned);
        }
        return closeResult;
    }

    private static long read64(byte[] data, int offset) {
        long value = 0;
        for (int i = 0; i < 8; i++) value = (value << 8) | (data[offset + i] & 0xffL);
        return value;
    }

    /** Returns null for malformed, stale or out-of-order messages. A callback can block this session. */
    public byte[] receive(byte[] input) {
        if (closeRequested.get() || (owner == null && !endpoint.allowsUnmanagedSession())) return null;
        receiveLock.lock();
        try {
            if (closeRequested.get() || (owner == null && !endpoint.allowsUnmanagedSession()) ||
                    dispatching || input == null || input.length < HEADER_SIZE || input.length > HEADER_SIZE + MAX_PAYLOAD)
                return null;
            byte[] frame = input.clone();
            if (frame[0] != 'R' || frame[1] != 'M' || frame[2] != 'E' || frame[3] != '1') return null;
            long sequence = read64(frame, 12);
            if (read64(frame, 4) != epoch || lastSequence == -1L || sequence != lastSequence + 1) return null;
            int kind = frame[20] & 0xff;
            int size = ((frame[21] & 0xff) << 8) | (frame[22] & 0xff);
            if ((kind != 1 && kind != 2 && kind != 4) || size == 0 || size > MAX_PAYLOAD || frame.length != HEADER_SIZE + size)
                return null;
            if (kind == 4 && (size != 16 || read64(frame, HEADER_SIZE) != epoch ||
                    read64(frame, HEADER_SIZE + 8) == 0)) return null;

            // Reserve before dispatch: a partial delivery must never be retried under the same sequence.
            lastSequence = sequence;
            final RadioEventEndpoint.Outcome outcome;
            dispatching = true;
            try { outcome = endpoint.dispatch(kind, Arrays.copyOfRange(frame, HEADER_SIZE, frame.length), epoch, sequence, this); }
            finally { dispatching = false; }
            int status = outcome == RadioEventEndpoint.Outcome.SUBMITTED ? 1 :
                    outcome == RadioEventEndpoint.Outcome.REJECTED ? 2 : 3;
            byte[] ack = Arrays.copyOf(frame, HEADER_SIZE + 1);
            ack[20] = 3; ack[21] = 0; ack[22] = 1; ack[23] = (byte) status;
            return ack;
        } finally { receiveLock.unlock(); }
    }
}
