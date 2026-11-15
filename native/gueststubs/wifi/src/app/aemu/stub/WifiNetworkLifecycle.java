package app.aemu.stub;

/** Serializes registration and removal so one connection owns at most one agent. */
final class WifiNetworkLifecycle {
    interface Connection { void stop() throws Exception; }
    /** start() must clean up on failure, or throw UncertainStartException with the agent. */
    interface Factory { Connection start() throws Exception; }
    enum Status { DISCONNECTED, CONNECTED, UNKNOWN }

    static final class UncertainStartException extends Exception {
        private final Connection agent;
        UncertainStartException(Connection agent, Throwable failure, Throwable cleanupFailure) {
            super("network registration cleanup could not be confirmed", failure);
            if (agent == null) throw new IllegalArgumentException("agent is required");
            this.agent = agent;
            addSuppressed(cleanupFailure);
        }
        Connection agent() { return agent; }
    }

    private final Factory factory;
    private Connection connection;
    private Status status = Status.DISCONNECTED;

    WifiNetworkLifecycle(Factory factory) {
        if (factory == null) throw new IllegalArgumentException("factory is required");
        this.factory = factory;
    }

    synchronized boolean isConnected() { return status == Status.CONNECTED; }
    synchronized Status status() { return status; }
    synchronized boolean hasUncertainAgent() { return status == Status.UNKNOWN && connection != null; }
    synchronized Connection connectedAgent() { return status == Status.CONNECTED ? connection : null; }

    synchronized void setConnected(boolean connected) throws Exception {
        if (status == Status.UNKNOWN) throw new IllegalStateException("network agent status is unknown");
        if (connected && status == Status.DISCONNECTED) {
            Connection started;
            try {
                started = factory.start();
            } catch (UncertainStartException failure) {
                connection = failure.agent();
                status = Status.UNKNOWN;
                throw failure;
            }
            if (started == null) {
                status = Status.UNKNOWN;
                throw new IllegalStateException("network registration returned no connection");
            }
            connection = started;
            status = Status.CONNECTED;
        } else if (!connected && status == Status.CONNECTED) {
            try {
                connection.stop();
            } catch (Exception | Error failure) {
                status = Status.UNKNOWN;
                throw failure;
            }
            connection = null;
            status = Status.DISCONNECTED;
        }
    }
}
