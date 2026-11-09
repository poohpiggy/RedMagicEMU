package app.aemu.stub;

import android.hardware.radio.RadioError;
import android.hardware.radio.RadioIndicationType;
import android.hardware.radio.RadioResponseInfo;
import android.hardware.radio.RadioResponseType;
import android.hardware.radio.voice.Call;
import android.hardware.radio.voice.CallForwardInfo;
import android.hardware.radio.voice.CdmaSignalInfoRecord;
import android.hardware.radio.voice.Dial;
import android.hardware.radio.voice.IRadioVoice;
import android.hardware.radio.voice.IRadioVoiceIndication;
import android.hardware.radio.voice.IRadioVoiceResponse;
import android.hardware.radio.voice.LastCallFailCauseInfo;
import android.os.IBinder;
import android.os.RemoteException;

import java.util.NoSuchElementException;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.Callable;
import java.util.concurrent.CancellationException;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.Future;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;

/** Unregistered single-slot voice service. Injection is local only; no host bridge is connected. */
public final class RadioVoiceService extends IRadioVoice.Stub implements AutoCloseable {
    private volatile RadioRequestCounter requestCounter;
    void setRequestCounterForExperiment(RadioRequestCounter counter) { requestCounter = counter; }
    @Override public boolean onTransact(int code, android.os.Parcel data, android.os.Parcel reply, int flags) throws RemoteException {
        RadioRequestCounter counter = requestCounter;
        if (counter != null) counter.record(code);
        return super.onTransact(code, data, reply, flags);
    }
    private final Object lock = new Object();
    private volatile Thread workerThread;
    private final ThreadPoolExecutor events;
    private final ThreadPoolExecutor detacher;
    private volatile Thread detachThread;
    private final long operationTimeoutMs;
    private final SyntheticSingleSlot syntheticSlot;
    private final Runnable deliveryListener;
    private boolean closed;
    private boolean stopping;
    private boolean eventEndpointClaimed, eventSessionOwnerClaimed, eventSessionClaimPending, eventSessionClaimUncertain;
    private long bindingGeneration;
    private int bindingsInFlight;
    private int registrationsInFlight;
    private int replaysInFlight;
    private int referencesInFlight;
    private final List<Callbacks> pendingDetach = new ArrayList<>();
    private int detaching;
    private boolean cleanupScheduled;
    private RuntimeException asynchronousCleanupFailure;
    private static final int MAX_BINDINGS = 64;

    RadioVoiceService(String slot) { this(slot, 64, 5000, null); }

    RadioVoiceService(String slot, int queueCapacity, long timeoutMs) {
        this(slot, queueCapacity, timeoutMs, null);
    }

    RadioVoiceService(String slot, int queueCapacity, long timeoutMs, SyntheticSingleSlot state) {
        if (!"slot1".equals(slot)) throw new IllegalArgumentException("only slot1 is supported");
        if (queueCapacity <= 0 || timeoutMs <= 0) throw new IllegalArgumentException("invalid queue configuration");
        syntheticSlot = state;
        operationTimeoutMs = timeoutMs;
        events = new ThreadPoolExecutor(1, 1, 0, TimeUnit.MILLISECONDS,
            new ArrayBlockingQueue<>(queueCapacity), runnable -> {
                Thread thread = new Thread(() -> { workerThread = Thread.currentThread(); runnable.run(); }, "rme-radio-voice");
                thread.setDaemon(true);
                return thread;
            }, new ThreadPoolExecutor.AbortPolicy());
        detacher = new ThreadPoolExecutor(1, 1, 0, TimeUnit.MILLISECONDS,
            new ArrayBlockingQueue<>(MAX_BINDINGS), runnable -> {
                Thread thread = new Thread(() -> { detachThread = Thread.currentThread(); runnable.run(); }, "rme-radio-detach");
                thread.setDaemon(true);
                return thread;
            }, new ThreadPoolExecutor.AbortPolicy());
        deliveryListener = state == null ? null : this::invalidateCall;
        if (deliveryListener != null) state.addDeliveryListener(deliveryListener);
    }
    private Callbacks callbacks;
    private String callNumber;
    private int callState = -1;
    private long callDeliveryToken;
    private long callEpoch, callSequence;
    private int pendingCallRemovalNotifications;

    /** A voice instance has exactly one event endpoint for its lifetime. */
    boolean claimEventEndpoint() {
        synchronized (lock) {
            if (closed || stopping || eventEndpointClaimed) return false;
            eventEndpointClaimed = true;
            return true;
        }
    }

    /** Claim only after every earlier voice operation has passed the ordered queue. */
    boolean claimEventSessionOwner() {
        synchronized (lock) {
            if (closed || stopping || eventSessionOwnerClaimed || eventSessionClaimPending || eventSessionClaimUncertain)
                return false;
            eventSessionClaimPending = true;
        }
        try {
            return ordered(() -> {
                synchronized (lock) {
                    if (closed || stopping || eventSessionOwnerClaimed || eventSessionClaimUncertain || callNumber != null)
                        return false;
                    eventSessionOwnerClaimed = true;
                    return true;
                }
            });
        } catch (RemoteException | QueueFull | RuntimeException failure) {
            synchronized (lock) { eventSessionClaimUncertain = true; }
            throw new IllegalStateException("event session owner claim uncertain; retire voice service", failure);
        } finally {
            synchronized (lock) { eventSessionClaimPending = false; }
        }
    }

    boolean allowsUnmanagedEventSession() {
        synchronized (lock) {
            return !eventSessionOwnerClaimed && !eventSessionClaimPending && !eventSessionClaimUncertain && !stopping && !closed;
        }
    }

    private boolean acceptsEventSourceLocked(boolean managed) {
        return !closed && !stopping && !eventSessionClaimPending && !eventSessionClaimUncertain &&
                eventSessionOwnerClaimed == managed;
    }

    /** Must hold lock; call identity never survives removal. */
    private void clearCallLocked() {
        callNumber = null; callState = -1; callDeliveryToken = 0; callEpoch = 0; callSequence = 0;
    }

    /** Clear stale calls synchronously; notify on the voice queue without holding the slot lock. */
    private void invalidateCall() {
        final Callbacks c;
        synchronized (lock) {
            if (stopping || closed || callNumber == null || syntheticSlot.isCurrentDelivery(callDeliveryToken)) return;
            clearCallLocked();
            c = callbacks;
            if (c == null || c.dead) return;
            c.references++; referencesInFlight++;
            pendingCallRemovalNotifications++;
        }
        try {
            events.execute(() -> {
                try { c.indication.callStateChanged(RadioIndicationType.UNSOLICITED); }
                catch (RemoteException | RuntimeException error) { recordStateFailure(error); }
                finally {
                    synchronized (lock) { pendingCallRemovalNotifications--; }
                    release(c);
                }
            });
        } catch (RejectedExecutionException error) {
            synchronized (lock) { pendingCallRemovalNotifications--; }
            release(c);
            synchronized (lock) { if (!stopping && !closed) recordStateFailure(error); }
        }
    }

    private void recordStateFailure(Exception error) {
        synchronized (lock) {
            if (asynchronousCleanupFailure == null)
                asynchronousCleanupFailure = new IllegalStateException("voice state notification failed", error);
        }
    }

    private static final class QueueFull extends Exception {
        final int error;
        QueueFull(int error) { this.error = error; }
    }

    /** Never hold the state lock across IPC. Timeout means the event may already have run. */
    private <T> T ordered(Callable<T> operation) throws RemoteException, QueueFull {
        return ordered(operation, null);
    }

    private <T> T ordered(Callable<T> operation, Callbacks lease) throws RemoteException, QueueFull {
        if (Thread.currentThread() == workerThread) {
            if (lease != null) release(lease);
            throw new RemoteException("reentrant radio voice request");
        }
        final Future<T> pending;
        try { pending = events.submit(() -> { try { return operation.call(); } finally { if (lease != null) release(lease); } }); }
        catch (RejectedExecutionException rejected) {
            throw new QueueFull(events.isShutdown() ? RadioError.RADIO_NOT_AVAILABLE : RadioError.NO_RESOURCES);
        }
        try { return pending.get(operationTimeoutMs, TimeUnit.MILLISECONDS); }
        catch (InterruptedException interrupted) {
            Thread.currentThread().interrupt();
            throw new RemoteException("radio voice interrupted; event may already have been submitted");
        } catch (TimeoutException timeout) {
            throw new RemoteException("radio voice timed out; event may already have been submitted");
        } catch (CancellationException cancelled) {
            throw new RemoteException("radio voice service stopped before the event completed");
        } catch (ExecutionException failed) {
            Throwable cause = failed.getCause();
            if (cause instanceof RemoteException) throw (RemoteException) cause;
            if (cause instanceof RuntimeException) throw (RuntimeException) cause;
            throw new RemoteException("radio voice worker failed: " + cause.getClass().getSimpleName());
        }
    }

    /** Waits for queued events, callback leases and registered replays. */
    @Override public void close() {
        if (Thread.currentThread() == workerThread || Thread.currentThread() == detachThread)
            throw new IllegalStateException("cannot close from a voice worker");
        long started = System.nanoTime();
        long budget = TimeUnit.MILLISECONDS.toNanos(operationTimeoutMs);
        synchronized (lock) { stopping = true; bindingGeneration++; events.shutdown(); }
        if (deliveryListener != null) syntheticSlot.removeDeliveryListener(deliveryListener);
        try {
            if (!events.awaitTermination(operationTimeoutMs, TimeUnit.MILLISECONDS))
                throw new IllegalStateException("voice worker did not stop; accepted events may still complete");
            synchronized (lock) {
                while (replaysInFlight != 0 || bindingsInFlight != 0 || registrationsInFlight != 0 || referencesInFlight != 0) {
                    long remaining = budget - (System.nanoTime() - started);
                    if (remaining <= 0) throw new IllegalStateException("voice replay did not stop");
                    TimeUnit.NANOSECONDS.timedWait(lock, remaining);
                }
                if (!closed) {
                    closed = true;
                    Callbacks old = callbacks; callbacks = null; clearCallLocked();
                    if (old != null) retireLocked(old);
                }
            }
            long remaining = budget - (System.nanoTime() - started);
            if (remaining <= 0) throw new IllegalStateException("voice cleanup deadline expired");
            if (!detacher.isShutdown()) {
                cleanupRetired(remaining);
                detacher.shutdown();
            }
            remaining = budget - (System.nanoTime() - started);
            if (remaining <= 0 || !detacher.awaitTermination(remaining, TimeUnit.NANOSECONDS))
                throw new IllegalStateException("voice detach worker did not stop");
        } catch (InterruptedException interrupted) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("interrupted while stopping voice worker", interrupted);
        }
    }

    private final class Callbacks implements IBinder.DeathRecipient {
        final IRadioVoiceResponse response;
        final IRadioVoiceIndication indication;
        final IBinder responseBinder;
        final IBinder indicationBinder;
        boolean responseLinked, indicationLinked, dead, replayRegistered, retired;
        int references;

        Callbacks(IRadioVoiceResponse r, IRadioVoiceIndication i) {
            response = r; indication = i; responseBinder = r.asBinder(); indicationBinder = i.asBinder();
        }
        @Override public void binderDied() {
            boolean cleanup = false;
            synchronized (lock) {
                dead = true;
                if (callbacks == this) {
                    callbacks = null;
                    clearCallLocked();
                    retireLocked(this);
                    cleanup = true;
                }
            }
            if (cleanup) cleanupRetired(TimeUnit.MILLISECONDS.toNanos(operationTimeoutMs));
        }
    }

    private static void unlinkOne(IBinder binder, IBinder.DeathRecipient recipient) {
        try { binder.unlinkToDeath(recipient, 0); }
        catch (NoSuchElementException expectedAfterDeath) { /* already detached */ }
    }

    private static void unlink(Callbacks c) {
        RuntimeException failure = null;
        if (c.responseLinked) {
            try { unlinkOne(c.responseBinder, c); c.responseLinked = false; }
            catch (RuntimeException error) { failure = error; }
        }
        if (c.indicationLinked) {
            try { unlinkOne(c.indicationBinder, c); c.indicationLinked = false; }
            catch (RuntimeException error) {
                if (failure == null) failure = error; else failure.addSuppressed(error);
            }
        }
        if (failure != null) throw failure;
    }

    private void cleanupRetiredWorker() {
        RuntimeException failure = null;
        int processed = 0;
        while (processed++ < MAX_BINDINGS) {
            Callbacks retired;
            synchronized (lock) {
                int index = -1;
                for (int i = 0; i < pendingDetach.size(); i++)
                    if (pendingDetach.get(i).references == 0) { index = i; break; }
                if (index < 0) break;
                retired = pendingDetach.remove(index); detaching++;
            }
            try { unlink(retired); }
            catch (RuntimeException error) {
                if (failure == null) failure = error; else failure.addSuppressed(error);
            }
            finally {
                synchronized (lock) {
                    detaching--;
                    if (retired.responseLinked || retired.indicationLinked) pendingDetach.add(retired);
                }
            }
            if (failure != null) break;
        }
        if (failure != null) throw failure;
        synchronized (lock) {
            for (Callbacks c : pendingDetach) if (c.references == 0)
                throw new IllegalStateException("voice detach work remains");
        }
    }

    private void cleanupRetired(long timeoutNanos) {
        // Reentrant local Binder calls leave cleanup for the outer detach worker.
        if (Thread.currentThread() == detachThread) return;
        final Future<?> future;
        try { future = detacher.submit(this::cleanupRetiredWorker); }
        catch (RejectedExecutionException full) { throw new IllegalStateException("voice cleanup queue full", full); }
        try { future.get(timeoutNanos, TimeUnit.NANOSECONDS); }
        catch (InterruptedException error) { Thread.currentThread().interrupt(); throw new IllegalStateException("voice cleanup interrupted", error); }
        catch (TimeoutException error) { throw new IllegalStateException("voice cleanup timed out; detach may still finish", error); }
        catch (ExecutionException error) {
            if (error.getCause() instanceof RuntimeException) throw (RuntimeException) error.getCause();
            throw new IllegalStateException("voice cleanup failed", error.getCause());
        }
    }

    @Override public void setResponseFunctions(IRadioVoiceResponse response,
            IRadioVoiceIndication indication) throws RemoteException {
        if (response == null || indication == null || response.asBinder() == null || indication.asBinder() == null)
            throw new RemoteException("both radio voice callbacks are required");
        synchronized (lock) {
            if (closed || stopping) throw new RemoteException("radio voice service is closed");
            registrationsInFlight++;
        }
        try { bindAndReplay(response, indication); }
        finally { synchronized (lock) { registrationsInFlight--; lock.notifyAll(); } }
    }

    private void bindAndReplay(IRadioVoiceResponse response, IRadioVoiceIndication indication) throws RemoteException {
        Callbacks next = bindCallbacks(response, indication);
        Throwable replayFailure = null;
        try {
            if (next.replayRegistered) {
                // Rebinding during an incoming call must prompt the new framework peer to query state.
                try {
                    ensureCurrent(next);
                    next.indication.callStateChanged(RadioIndicationType.UNSOLICITED);
                    ensureCurrent(next);
                } finally {
                    synchronized (lock) { replaysInFlight--; lock.notifyAll(); }
                    release(next);
                }
            }
        } catch (RemoteException | RuntimeException error) {
            replayFailure = error;
            throw error;
        } finally {
            try { cleanupRetired(TimeUnit.MILLISECONDS.toNanos(operationTimeoutMs)); }
            catch (RuntimeException cleanup) {
                if (replayFailure != null) replayFailure.addSuppressed(cleanup); else throw cleanup;
            }
        }
    }

    private Callbacks bindCallbacks(IRadioVoiceResponse response, IRadioVoiceIndication indication) throws RemoteException {
        Callbacks next = new Callbacks(response, indication);
        long generation;
        synchronized (lock) {
            if (closed || stopping) throw new RemoteException("radio voice service is closed");
            if (pendingDetach.size() + detaching + bindingsInFlight >= MAX_BINDINGS)
                throw new RemoteException("radio voice callback capacity exhausted");
            bindingsInFlight++; generation = ++bindingGeneration;
        }
        boolean published = false;
        boolean cleanupQueued = false;
        try {
            next.responseBinder.linkToDeath(next, 0); next.responseLinked = true;
            next.indicationBinder.linkToDeath(next, 0); next.indicationLinked = true;
            synchronized (lock) {
                if (next.dead || stopping || generation != bindingGeneration)
                    throw new RemoteException("radio voice callback no longer current");
                Callbacks old = callbacks;
                callbacks = next; published = true;
                if (old != null) retireLocked(old);
                if (callNumber != null || pendingCallRemovalNotifications != 0) {
                    next.replayRegistered = true; replaysInFlight++;
                    next.references++; referencesInFlight++;
                }
            }
            return next;
        } catch (RemoteException | RuntimeException error) {
            synchronized (lock) {
                if (next.responseLinked || next.indicationLinked) {
                    retireLocked(next); cleanupQueued = true;
                }
            }
            if (cleanupQueued) {
                try { cleanupRetired(TimeUnit.MILLISECONDS.toNanos(operationTimeoutMs)); }
                catch (RuntimeException cleanup) { error.addSuppressed(cleanup); }
            }
            throw error;
        } finally {
            synchronized (lock) {
                if (!published && !cleanupQueued && (next.responseLinked || next.indicationLinked)) retireLocked(next);
                bindingsInFlight--; lock.notifyAll();
            }
        }
    }

    private Callbacks connected() throws RemoteException {
        synchronized (lock) {
            if (closed || callbacks == null || callbacks.dead) throw new RemoteException("radio voice is not connected");
            return callbacks;
        }
    }

    private void retireLocked(Callbacks c) {
        if (!c.retired) { c.retired = true; pendingDetach.add(c); }
    }

    private Callbacks retain() throws RemoteException {
        synchronized (lock) {
            Callbacks c = connected(); c.references++; referencesInFlight++; return c;
        }
    }

    private void release(Callbacks c) {
        boolean schedule = false;
        synchronized (lock) {
            c.references--; referencesInFlight--; lock.notifyAll();
            if (c.retired && c.references == 0 && !cleanupScheduled) { cleanupScheduled = true; schedule = true; }
        }
        if (schedule) scheduleCleanup();
    }

    private void scheduleCleanup() {
        try {
            detacher.execute(() -> {
                RuntimeException failure = null;
                try { cleanupRetiredWorker(); } catch (RuntimeException error) { failure = error; }
                boolean again = false;
                synchronized (lock) {
                    if (failure != null && asynchronousCleanupFailure == null) asynchronousCleanupFailure = failure;
                    cleanupScheduled = false;
                    if (failure == null) for (Callbacks c : pendingDetach) if (c.references == 0) {
                        cleanupScheduled = true; again = true; break;
                    }
                }
                if (again) scheduleCleanup();
            });
        } catch (RejectedExecutionException error) {
            synchronized (lock) {
                cleanupScheduled = false;
                if (asynchronousCleanupFailure == null)
                    asynchronousCleanupFailure = new IllegalStateException("voice cleanup queue full", error);
            }
        }
    }

    /** Sticky diagnostic for failures from deferred cleanup; callers must surface it. */
    public RuntimeException getAsynchronousCleanupFailure() {
        synchronized (lock) { return asynchronousCleanupFailure; }
    }

    private interface ResponseCall { void invoke(IRadioVoiceResponse response) throws RemoteException; }
    private void direct(ResponseCall call) throws RemoteException {
        Callbacks c = retain();
        try { call.invoke(c.response); } finally { release(c); }
    }

    private void ensureCurrent(Callbacks expected) throws RemoteException {
        synchronized (lock) {
            if (closed || callbacks != expected || expected.dead)
                throw new RemoteException("radio callback changed; delivery may be partial");
        }
    }

    private static RadioResponseInfo unsupported(int serial) {
        return result(serial, RadioError.REQUEST_NOT_SUPPORTED);
    }

    private static RadioResponseInfo result(int serial, int error) {
        RadioResponseInfo info = new RadioResponseInfo();
        info.type = RadioResponseType.SOLICITED;
        info.serial = serial;
        info.error = error;
        return info;
    }

    /** Receives one virtual incoming call. No actual phone number is contacted. */
    public boolean injectIncoming(String number) throws RemoteException {
        return injectIncomingInternal(number, 0, 0, false);
    }

    /** Internal event identity; zero is reserved for the unassociated test-only entry point. */
    boolean injectIncoming(String number, long epoch, long sequence) throws RemoteException {
        return injectIncoming(number, epoch, sequence, false);
    }

    boolean injectIncoming(String number, long epoch, long sequence, boolean managed) throws RemoteException {
        if (epoch == 0 || sequence == 0) return false;
        return injectIncomingInternal(number, epoch, sequence, managed);
    }

    private boolean injectIncomingInternal(String number, long epoch, long sequence, boolean managed) throws RemoteException {
        if (number == null || !number.matches("\\+?[0-9]{1,32}")) return false;
        final long token = syntheticSlot == null ? 0 : syntheticSlot.deliveryToken();
        if (syntheticSlot != null && token == 0)
            throw new RemoteException("virtual radio is not ready for voice");
        try { return ordered(() -> injectOrdered(number, token, epoch, sequence, managed)); }
        catch (QueueFull rejected) { throw new RemoteException("radio voice cannot accept the call event"); }
    }

    private boolean injectOrdered(String number, long token, long epoch, long sequence, boolean managed) throws RemoteException {
        if (syntheticSlot != null && !syntheticSlot.isCurrentDelivery(token))
            throw new RemoteException("virtual radio is not ready for voice");
        Callbacks c = retain();
        try {
            synchronized (lock) {
                if (callbacks != c || c.dead) throw new RemoteException("radio callback changed");
                if (!acceptsEventSourceLocked(managed)) return false;
                if (callNumber != null) return false;
                callNumber = number;
                callState = Call.STATE_INCOMING;
                callDeliveryToken = token;
                callEpoch = epoch; callSequence = sequence;
            }
            if (syntheticSlot != null && !syntheticSlot.isCurrentDelivery(token)) {
                synchronized (lock) {
                    if (callDeliveryToken == token) clearCallLocked();
                }
                throw new RemoteException("virtual radio changed before call notification");
            }
            // A failed write may still have delivered the preceding indication; never pretend to undo it.
            c.indication.callStateChanged(RadioIndicationType.UNSOLICITED);
            ensureCurrent(c);
            if (syntheticSlot != null && !syntheticSlot.isCurrentDelivery(token))
                throw new RemoteException("virtual radio changed; call event may be partially delivered");
            c.indication.callRing(RadioIndicationType.UNSOLICITED, true, new CdmaSignalInfoRecord());
            ensureCurrent(c);
            if (syntheticSlot != null && !syntheticSlot.isCurrentDelivery(token))
                throw new RemoteException("virtual radio changed; call event may be partially delivered");
            return true;
        } finally { release(c); }
    }

    /** End only the call created by the matching authenticated event identity. */
    boolean injectRemoteHangup(long epoch, long sequence) throws RemoteException {
        return injectRemoteHangup(epoch, sequence, false);
    }

    boolean injectRemoteHangup(long epoch, long sequence, boolean managed) throws RemoteException {
        if (epoch == 0 || sequence == 0) return false;
        try { return ordered(() -> remoteHangupOrdered(epoch, sequence, managed)); }
        catch (QueueFull rejected) { throw new RemoteException("radio voice cannot accept the hangup event"); }
    }

    /** Terminate only the call belonging to this ended event session. */
    boolean closeEventSession(long epoch) throws RemoteException {
        if (epoch == 0) return false;
        try { return ordered(() -> closeEventSessionOrdered(epoch)); }
        catch (QueueFull rejected) { throw new RemoteException("radio voice cannot accept session cleanup"); }
    }

    private boolean closeEventSessionOrdered(long epoch) throws RemoteException {
        final Callbacks c;
        synchronized (lock) {
            if (callNumber == null || callEpoch != epoch) return false;
            clearCallLocked();
            if (closed || stopping || callbacks == null || callbacks.dead)
                throw new RemoteException("session call removed; notification unavailable");
            c = callbacks; c.references++; referencesInFlight++;
            // Rebinding while this removal is being signalled must replay call state.
            pendingCallRemovalNotifications++;
        }
        try {
            c.indication.callStateChanged(RadioIndicationType.UNSOLICITED);
            ensureCurrent(c);
            return true;
        } finally {
            synchronized (lock) { pendingCallRemovalNotifications--; }
            release(c);
        }
    }

    private boolean remoteHangupOrdered(long epoch, long sequence, boolean managed) throws RemoteException {
        final Callbacks c;
        final long token;
        synchronized (lock) {
            if (!acceptsEventSourceLocked(managed)) return false;
            if (callNumber == null || callEpoch != epoch || callSequence != sequence) return false;
            if (syntheticSlot != null && !syntheticSlot.isCurrentDelivery(callDeliveryToken)) return false;
            if (closed || stopping || callbacks == null || callbacks.dead)
                throw new RemoteException("radio voice is not connected");
            c = callbacks; c.references++; referencesInFlight++;
            token = callDeliveryToken;
            // A callback rebound after the clear must still be told to query the empty call list.
            pendingCallRemovalNotifications++;
            clearCallLocked();
        }
        try {
            if (syntheticSlot != null && !syntheticSlot.isCurrentDelivery(token))
                throw new RemoteException("virtual radio changed; hangup may be partially delivered");
            c.indication.callStateChanged(RadioIndicationType.UNSOLICITED);
            ensureCurrent(c);
            if (syntheticSlot != null && !syntheticSlot.isCurrentDelivery(token))
                throw new RemoteException("virtual radio changed; hangup may be partially delivered");
            return true;
        } finally {
            synchronized (lock) { pendingCallRemovalNotifications--; }
            release(c);
        }
    }

    boolean usesState(SyntheticSingleSlot state) { return state != null && syntheticSlot == state; }

    private Call currentCall() {
        Call c = new Call();
        c.index = 1; c.state = callState; c.number = callNumber;
        c.toa = callNumber.startsWith("+") ? 145 : 129;
        c.isMT = true; c.isVoice = true; c.numberPresentation = Call.PRESENTATION_ALLOWED;
        c.name = ""; c.namePresentation = Call.PRESENTATION_UNKNOWN;
        c.uusInfo = new android.hardware.radio.voice.UusInfo[0];
        c.forwardedNumber = "";
        return c;
    }

    @Override public int getInterfaceVersion() { return IRadioVoice.VERSION; }
    @Override public String getInterfaceHash() { return IRadioVoice.HASH; }
    @Override public void responseAcknowledgement() { /* no outstanding indications */ }
    @Override public void acceptCall(int s) throws RemoteException {
        Callbacks c = retain();
        try { ordered(() -> { acceptOrdered(s, c); return null; }, c); }
        catch (QueueFull rejected) { try { c.response.acceptCallResponse(result(s, rejected.error)); } finally { release(c); } }
    }
    private void acceptOrdered(int s, Callbacks c) throws RemoteException {
        int error;
        synchronized (lock) {
            error = callNumber != null && callState == Call.STATE_INCOMING ? RadioError.NONE : RadioError.INVALID_STATE;
            if (error == RadioError.NONE) callState = Call.STATE_ACTIVE;
        }
        c.response.acceptCallResponse(result(s, error));
        if (error == RadioError.NONE) c.indication.callStateChanged(RadioIndicationType.UNSOLICITED);
    }
    @Override public void cancelPendingUssd(int s) throws RemoteException { direct(peer -> peer.cancelPendingUssdResponse(unsupported(s))); }
    @Override public void conference(int s) throws RemoteException { direct(peer -> peer.conferenceResponse(unsupported(s))); }
    @Override public void dial(int s, Dial d) throws RemoteException { direct(peer -> peer.dialResponse(unsupported(s))); }
    @Override public void emergencyDial(int s, Dial d, int c, String[] u, int r, boolean k, boolean t) throws RemoteException { direct(peer -> peer.emergencyDialResponse(unsupported(s))); }
    @Override public void exitEmergencyCallbackMode(int s) throws RemoteException { direct(peer -> peer.exitEmergencyCallbackModeResponse(unsupported(s))); }
    @Override public void explicitCallTransfer(int s) throws RemoteException { direct(peer -> peer.explicitCallTransferResponse(unsupported(s))); }
    @Override public void getCallForwardStatus(int s, CallForwardInfo c) throws RemoteException { direct(peer -> peer.getCallForwardStatusResponse(unsupported(s), new CallForwardInfo[0])); }
    @Override public void getCallWaiting(int s, int c) throws RemoteException { direct(peer -> peer.getCallWaitingResponse(unsupported(s), false, 0)); }
    @Override public void getClip(int s) throws RemoteException { direct(peer -> peer.getClipResponse(unsupported(s), 0)); }
    @Override public void getClir(int s) throws RemoteException { direct(peer -> peer.getClirResponse(unsupported(s), 0, 0)); }
    @Override public void getCurrentCalls(int s) throws RemoteException {
        Callbacks c = retain();
        try { ordered(() -> { getCallsOrdered(s, c); return null; }, c); }
        catch (QueueFull rejected) { try { c.response.getCurrentCallsResponse(result(s, rejected.error), new Call[0]); } finally { release(c); } }
    }
    private void getCallsOrdered(int s, Callbacks c) throws RemoteException {
        Call[] calls;
        synchronized (lock) { calls = callNumber == null ? new Call[0] : new Call[]{currentCall()}; }
        c.response.getCurrentCallsResponse(result(s, RadioError.NONE), calls);
    }
    @Override public void getLastCallFailCause(int s) throws RemoteException { direct(peer -> peer.getLastCallFailCauseResponse(unsupported(s), new LastCallFailCauseInfo())); }
    @Override public void getMute(int s) throws RemoteException { direct(peer -> peer.getMuteResponse(unsupported(s), false)); }
    @Override public void getPreferredVoicePrivacy(int s) throws RemoteException { direct(peer -> peer.getPreferredVoicePrivacyResponse(unsupported(s), false)); }
    @Override public void getTtyMode(int s) throws RemoteException { direct(peer -> peer.getTtyModeResponse(unsupported(s), 0)); }
    @Override public void handleStkCallSetupRequestFromSim(int s, boolean a) throws RemoteException { direct(peer -> peer.handleStkCallSetupRequestFromSimResponse(unsupported(s))); }
    @Override public void hangup(int s, int i) throws RemoteException {
        Callbacks c = retain();
        try { ordered(() -> { hangupOrdered(s, i, c); return null; }, c); }
        catch (QueueFull rejected) { try { c.response.hangupConnectionResponse(result(s, rejected.error)); } finally { release(c); } }
    }
    private void hangupOrdered(int s, int i, Callbacks c) throws RemoteException {
        int error;
        synchronized (lock) {
            error = i == 1 && callNumber != null ? RadioError.NONE : RadioError.INVALID_CALL_ID;
            if (error == RadioError.NONE) clearCallLocked();
        }
        c.response.hangupConnectionResponse(result(s, error));
        if (error == RadioError.NONE) c.indication.callStateChanged(RadioIndicationType.UNSOLICITED);
    }
    @Override public void hangupForegroundResumeBackground(int s) throws RemoteException {
        Callbacks c = retain();
        try { ordered(() -> { hangupForegroundOrdered(s, c); return null; }, c); }
        catch (QueueFull rejected) { try { c.response.hangupForegroundResumeBackgroundResponse(result(s, rejected.error)); } finally { release(c); } }
    }
    private void hangupForegroundOrdered(int s, Callbacks c) throws RemoteException {
        int error;
        synchronized (lock) {
            error = callNumber != null && callState == Call.STATE_ACTIVE ? RadioError.NONE : RadioError.INVALID_STATE;
            if (error == RadioError.NONE) clearCallLocked();
        }
        c.response.hangupForegroundResumeBackgroundResponse(result(s, error));
        if (error == RadioError.NONE) c.indication.callStateChanged(RadioIndicationType.UNSOLICITED);
    }
    @Override public void hangupWaitingOrBackground(int s) throws RemoteException {
        Callbacks c = retain();
        try { ordered(() -> { hangupWaitingOrBackgroundOrdered(s, c); return null; }, c); }
        catch (QueueFull rejected) { try { c.response.hangupWaitingOrBackgroundResponse(result(s, rejected.error)); } finally { release(c); } }
    }
    private void hangupWaitingOrBackgroundOrdered(int s, Callbacks c) throws RemoteException {
        int error;
        synchronized (lock) {
            error = callNumber != null && callState == Call.STATE_INCOMING ? RadioError.NONE : RadioError.INVALID_STATE;
            if (error == RadioError.NONE) clearCallLocked();
        }
        c.response.hangupWaitingOrBackgroundResponse(result(s, error));
        if (error == RadioError.NONE) c.indication.callStateChanged(RadioIndicationType.UNSOLICITED);
    }
    @Override public void isVoNrEnabled(int s) throws RemoteException { direct(peer -> peer.isVoNrEnabledResponse(unsupported(s), false)); }
    @Override public void rejectCall(int s) throws RemoteException {
        Callbacks c = retain();
        try { ordered(() -> { rejectOrdered(s, c); return null; }, c); }
        catch (QueueFull rejected) { try { c.response.rejectCallResponse(result(s, rejected.error)); } finally { release(c); } }
    }
    private void rejectOrdered(int s, Callbacks c) throws RemoteException {
        int error;
        synchronized (lock) {
            error = callNumber != null && callState == Call.STATE_INCOMING ? RadioError.NONE : RadioError.INVALID_STATE;
            if (error == RadioError.NONE) clearCallLocked();
        }
        c.response.rejectCallResponse(result(s, error));
        if (error == RadioError.NONE) c.indication.callStateChanged(RadioIndicationType.UNSOLICITED);
    }
    @Override public void sendBurstDtmf(int s, String d, int on, int off) throws RemoteException { direct(peer -> peer.sendBurstDtmfResponse(unsupported(s))); }
    @Override public void sendCdmaFeatureCode(int s, String c) throws RemoteException { direct(peer -> peer.sendCdmaFeatureCodeResponse(unsupported(s))); }
    @Override public void sendDtmf(int s, String d) throws RemoteException { direct(peer -> peer.sendDtmfResponse(unsupported(s))); }
    @Override public void sendUssd(int s, String u) throws RemoteException { direct(peer -> peer.sendUssdResponse(unsupported(s))); }
    @Override public void separateConnection(int s, int i) throws RemoteException { direct(peer -> peer.separateConnectionResponse(unsupported(s))); }
    @Override public void setCallForward(int s, CallForwardInfo c) throws RemoteException { direct(peer -> peer.setCallForwardResponse(unsupported(s))); }
    @Override public void setCallWaiting(int s, boolean e, int c) throws RemoteException { direct(peer -> peer.setCallWaitingResponse(unsupported(s))); }
    @Override public void setClir(int s, int c) throws RemoteException { direct(peer -> peer.setClirResponse(unsupported(s))); }
    @Override public void setMute(int s, boolean e) throws RemoteException { direct(peer -> peer.setMuteResponse(unsupported(s))); }
    @Override public void setPreferredVoicePrivacy(int s, boolean e) throws RemoteException { direct(peer -> peer.setPreferredVoicePrivacyResponse(unsupported(s))); }
    @Override public void setTtyMode(int s, int m) throws RemoteException { direct(peer -> peer.setTtyModeResponse(unsupported(s))); }
    @Override public void setVoNrEnabled(int s, boolean e) throws RemoteException { direct(peer -> peer.setVoNrEnabledResponse(unsupported(s))); }
    @Override public void startDtmf(int s, String d) throws RemoteException { direct(peer -> peer.startDtmfResponse(unsupported(s))); }
    @Override public void stopDtmf(int s) throws RemoteException { direct(peer -> peer.stopDtmfResponse(unsupported(s))); }
    @Override public void switchWaitingOrHoldingAndActive(int s) throws RemoteException { direct(peer -> peer.switchWaitingOrHoldingAndActiveResponse(unsupported(s))); }
}
