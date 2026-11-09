package app.aemu.stub;

import android.os.RemoteException;
import java.nio.charset.StandardCharsets;

/** Isolated model adapter. SUBMITTED does not imply framework or user delivery. */
public final class RadioEventEndpoint {
    public enum Outcome { SUBMITTED, REJECTED, UNCERTAIN }
    private final RadioVoiceService voice;
    private final RadioMessagingService messaging;
    private final SyntheticSingleSlot slot;
    private final Object sessionGate = new Object();
    private boolean legacyRetired;
    private int legacyInFlight;
    private RadioEventSessionManager manager;

    RadioEventEndpoint(SyntheticSingleSlot slot, RadioVoiceService voice, RadioMessagingService messaging) {
        if (slot == null || voice == null || messaging == null ||
                !voice.usesState(slot) || !messaging.usesState(slot))
            throw new IllegalArgumentException("both radio services must use the same single-slot state");
        if (!voice.claimEventEndpoint()) throw new IllegalStateException("voice event endpoint unavailable");
        this.slot = slot;
        this.voice = voice; this.messaging = messaging;
    }

    /** Isolated legacy contract tests only; never use this factory for a registered service. */
    static RadioEventEndpoint withoutStateForTests(RadioVoiceService voice, RadioMessagingService messaging) {
        return new RadioEventEndpoint(voice, messaging);
    }

    private RadioEventEndpoint(RadioVoiceService voice, RadioMessagingService messaging) {
        if (voice == null || messaging == null) throw new IllegalArgumentException("both radio models are required");
        if (!voice.claimEventEndpoint()) throw new IllegalStateException("voice event endpoint unavailable");
        this.slot = null;
        this.voice = voice; this.messaging = messaging;
    }

    boolean claimSessionOwner(RadioEventSessionManager owner) {
        synchronized (sessionGate) {
            if (legacyRetired || legacyInFlight != 0) return false;
            // Retire legacy entry before the queue barrier; an uncertain claim stays closed.
            legacyRetired = true;
        }
        if (!voice.claimEventSessionOwner()) return false;
        synchronized (sessionGate) { manager = owner; }
        return true;
    }

    boolean allowsUnmanagedSession() {
        synchronized (sessionGate) { return !legacyRetired && voice.allowsUnmanagedEventSession(); }
    }

    private boolean beginLegacy() {
        synchronized (sessionGate) {
            if (legacyRetired || !voice.allowsUnmanagedEventSession()) return false;
            legacyInFlight++;
            return true;
        }
    }

    private void endLegacy() { synchronized (sessionGate) { legacyInFlight--; } }

    /** Cleanup is independent of delivery readiness, but must belong to this endpoint. */
    void closeEventSession(RadioEventSession source) throws RemoteException {
        RadioEventSessionManager sessionOwner = source.owner();
        if (sessionOwner == null) {
            if (!beginLegacy()) throw new RemoteException("unmanaged session is retired");
            try { voice.closeEventSession(source.epoch()); }
            finally { endLegacy(); }
            return;
        }
        synchronized (sessionGate) {
            if (manager != sessionOwner) throw new RemoteException("session owner mismatch");
        }
        if (!sessionOwner.isActive(source)) throw new RemoteException("session is not active");
        voice.closeEventSession(source.epoch());
    }

    public Outcome dispatch(int kind, byte[] payload) {
        if (!beginLegacy()) return Outcome.REJECTED;
        try { return dispatchInternal(kind, payload, 0, 0, false, false); }
        finally { endLegacy(); }
    }

    /** The authenticated frame identity also identifies a submitted incoming call. */
    Outcome dispatch(int kind, byte[] payload, long epoch, long sequence, RadioEventSession source) {
        if (source == null || epoch == 0 || sequence == 0 || source.epoch() != epoch) return Outcome.REJECTED;
        RadioEventSessionManager sessionOwner = source == null ? null : source.owner();
        if (sessionOwner == null) {
            if (!beginLegacy()) return Outcome.REJECTED;
            try { return dispatchInternal(kind, payload, epoch, sequence, true, false); }
            finally { endLegacy(); }
        }
        synchronized (sessionGate) { if (manager != sessionOwner) return Outcome.REJECTED; }
        if (!sessionOwner.isActive(source)) return Outcome.REJECTED;
        return dispatchInternal(kind, payload, epoch, sequence, true, true);
    }

    private Outcome dispatchInternal(int kind, byte[] payload, long epoch, long sequence, boolean associated, boolean managed) {
        if (payload == null) return Outcome.REJECTED;
        if (slot != null && !slot.canDeliverVoiceOrSms()) return Outcome.REJECTED;
        if (kind == 1) {
            if (payload.length == 0 || payload.length > 256) return Outcome.REJECTED;
            try { return messaging.tryInjectIncomingGsmSms(payload) ? Outcome.SUBMITTED : Outcome.REJECTED; }
            catch (RemoteException | RuntimeException failure) { return Outcome.UNCERTAIN; }
        }
        if (kind == 2) {
            if (!validNumber(payload)) return Outcome.REJECTED;
            try {
                String number = new String(payload, StandardCharsets.US_ASCII);
                return (associated ? voice.injectIncoming(number, epoch, sequence, managed) : voice.injectIncoming(number))
                        ? Outcome.SUBMITTED : Outcome.REJECTED;
            } catch (RemoteException | RuntimeException failure) { return Outcome.UNCERTAIN; }
        }
        if (kind == 4 && associated) {
            if (payload.length != 16 || read64(payload, 0) != epoch) return Outcome.REJECTED;
            long callSequence = read64(payload, 8);
            if (callSequence == 0) return Outcome.REJECTED;
            try {
                return voice.injectRemoteHangup(epoch, callSequence, managed) ? Outcome.SUBMITTED : Outcome.REJECTED;
            } catch (RemoteException | RuntimeException failure) { return Outcome.UNCERTAIN; }
        }
        return Outcome.REJECTED;
    }

    private static long read64(byte[] bytes, int offset) {
        long result = 0;
        for (int i = 0; i < 8; i++) result = (result << 8) | (bytes[offset + i] & 0xffL);
        return result;
    }

    private static boolean validNumber(byte[] payload) {
        int first = payload.length > 0 && payload[0] == '+' ? 1 : 0;
        int digits = payload.length - first;
        if (digits < 1 || digits > 32) return false;
        for (int i = first; i < payload.length; i++) if (payload[i] < '0' || payload[i] > '9') return false;
        return true;
    }
}
