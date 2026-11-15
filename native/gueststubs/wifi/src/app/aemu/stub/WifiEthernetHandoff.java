package app.aemu.stub;

/** Coordinates an explicit stop, confirmed removal, and reversible Wi-Fi startup. */
final class WifiEthernetHandoff {
    interface Backend {
        Object oldNetwork() throws Exception;
        void stopEthernet() throws Exception;
        boolean waitGone(Object network) throws Exception;
        void startWifi() throws Exception;
        boolean cleanupWifi() throws Exception;
        void restoreEthernet() throws Exception;
        boolean waitEthernetRestored() throws Exception;
    }
    static final class FailedAndRestored extends Exception {
        FailedAndRestored(Throwable cause) { super("Wi-Fi failed; Ethernet restored", cause); }
    }
    static final class Uncertain extends Exception {
        Uncertain(String message, Throwable cause) { super(message, cause); }
    }
    private final Backend backend;
    WifiEthernetHandoff(Backend backend) {
        if (backend == null) throw new IllegalArgumentException("handoff backend required");
        this.backend = backend;
    }

    void run() throws Exception {
        Object old = backend.oldNetwork();
        if (old == null) throw new IllegalStateException("old Ethernet Network unavailable");
        try {
            backend.stopEthernet();
            if (!backend.waitGone(old)) throw new IllegalStateException("old Ethernet is still present");
        } catch (Exception failure) {
            throw new Uncertain("Ethernet removal unconfirmed; no replacement started", failure);
        }
        try {
            backend.startWifi();
        } catch (Exception failure) {
            try {
                if (!backend.cleanupWifi()) throw new IllegalStateException("Wi-Fi cleanup unconfirmed");
                backend.restoreEthernet();
                if (!backend.waitEthernetRestored()) throw new IllegalStateException("Ethernet restoration unconfirmed");
            } catch (Exception recoveryFailure) {
                Uncertain uncertain = new Uncertain("Wi-Fi failed and Ethernet restoration is unconfirmed", failure);
                uncertain.addSuppressed(recoveryFailure);
                throw uncertain;
            }
            throw new FailedAndRestored(failure);
        }
    }
}
