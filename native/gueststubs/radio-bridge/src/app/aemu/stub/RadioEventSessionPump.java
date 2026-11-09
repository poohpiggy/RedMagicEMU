package app.aemu.stub;

import java.io.IOException;

/** Isolated framed-transport adapter; transport authentication is the caller's responsibility. */
final class RadioEventSessionPump {
    interface FrameTransport {
        /** Returns one complete frame, or null on EOF. */
        byte[] read() throws IOException;
        void write(byte[] frame) throws IOException;
    }

    static final class Result {
        final IOException transportFailure;
        final RadioEventSession.CloseResult closeResult;
        final Throwable closeFailure;

        Result(IOException transportFailure, RadioEventSession.CloseResult closeResult, Throwable closeFailure) {
            this.transportFailure = transportFailure;
            this.closeResult = closeResult;
            this.closeFailure = closeFailure;
        }

        boolean isClean() {
            return transportFailure == null && closeResult == RadioEventSession.CloseResult.CLEANED;
        }
    }

    /** Returns failures to the caller; an unchecked exception still propagates after session cleanup. */
    static Result run(RadioEventSession session, FrameTransport transport) {
        if (session == null || transport == null) throw new IllegalArgumentException("session and transport required");
        IOException failure = null;
        Throwable thrown = null;
        RadioEventSession.CloseResult closed;
        try {
            for (;;) {
                byte[] frame = transport.read();
                if (frame == null) break;
                byte[] ack = session.receive(frame);
                if (ack == null) throw new IOException("event frame rejected");
                transport.write(ack);
            }
        } catch (IOException error) {
            failure = error;
        } catch (RuntimeException | Error error) {
            thrown = error;
            throw error;
        } finally {
            // Includes unchecked exceptions and Errors; do not retry an uncertain cleanup.
            try {
                closed = session.closeSession();
            } catch (RuntimeException | Error closeError) {
                Throwable prior = thrown != null ? thrown : failure;
                if (prior != null && prior != closeError) closeError.addSuppressed(prior);
                throw closeError;
            }
        }
        return new Result(failure, closed, session.closeFailure());
    }
}
