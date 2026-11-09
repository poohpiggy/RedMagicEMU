package app.aemu.stub;

/** Isolated owner; one per voice service, and not connected to production startup. */
final class RadioEventSessionManager {
    static final class EpochAllocator {
        private long last;
        EpochAllocator(long last) { this.last = last; }
        synchronized long next() {
            if (last == -1L) throw new IllegalStateException("event epoch space exhausted");
            return ++last;
        }
    }

    // Shared by all managers loaded by this class loader. No cross-process persistence.
    private static final EpochAllocator EPOCHS = new EpochAllocator(0);
    private final RadioEventEndpoint endpoint;
    private RadioEventSession active;
    private boolean exitRequired;
    private boolean externallyOpened;
    private boolean openedAny;

    RadioEventSessionManager(RadioEventEndpoint endpoint) {
        if (endpoint == null) throw new IllegalArgumentException("endpoint required");
        this.endpoint = endpoint;
        if (!endpoint.claimSessionOwner(this)) throw new IllegalStateException("voice session owner unavailable; reset may be required");
    }

    synchronized RadioEventSession open() {
        if (externallyOpened || exitRequired || active != null) throw new IllegalStateException("prior session is active or requires process exit");
        openedAny = true;
        active = new RadioEventSession(EPOCHS.next(), endpoint, this);
        return active;
    }

    /** One host-supplied random epoch for this service process; never reconnect in place. */
    synchronized RadioEventSession openOnce(long epoch) {
        if (epoch <= 0 || openedAny || externallyOpened || exitRequired || active != null)
            throw new IllegalStateException("external session cannot be opened");
        openedAny = true;
        externallyOpened = true;
        active = new RadioEventSession(epoch, endpoint, this);
        return active;
    }

    synchronized void finished(RadioEventSession session, boolean cleaned) {
        if (active != session) { exitRequired = true; return; }
        if (cleaned) active = null;
        else exitRequired = true;
    }

    synchronized boolean mustExitProcess() { return exitRequired; }

    synchronized boolean isActive(RadioEventSession session) { return !exitRequired && active == session; }
}
