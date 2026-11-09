package app.aemu.stub;

import java.util.ArrayList;
import java.util.List;

/** Synthetic in-process session disconnect and fresh service state, without Android IPC. */
public final class RadioEventDisconnectRestartTest {
    private static void check(boolean value, String message) {
        if (!value) throw new AssertionError(message);
    }

    private static byte[] frame(long epoch, long sequence, int kind, byte[] payload) {
        byte[] result = new byte[23 + payload.length];
        result[0] = 'R'; result[1] = 'M'; result[2] = 'E'; result[3] = '1';
        for (int i = 7; i >= 0; --i) {
            result[4 + i] = (byte) epoch; epoch >>>= 8;
            result[12 + i] = (byte) sequence; sequence >>>= 8;
        }
        result[20] = (byte) kind;
        result[21] = (byte) (payload.length >>> 8); result[22] = (byte) payload.length;
        System.arraycopy(payload, 0, result, 23, payload.length);
        return result;
    }

    private static byte[] sms(long epoch, long sequence) {
        return frame(epoch, sequence, 1, new byte[]{0, 4, 11});
    }
    private static byte[] call(long epoch, long sequence) {
        return frame(epoch, sequence, 2, new byte[]{'+', '1', '0', '0', '8', '6'});
    }

    public static void main(String[] args) throws Exception {
        final long oldEpoch = 501, newEpoch = 502;
        final byte[] oldSms = sms(oldEpoch, 1), oldCall = call(oldEpoch, 2);
        try (RadioEventSharedStateTest.Fixture first = new RadioEventSharedStateTest.Fixture(
                new RadioEventSharedStateTest.ReadyFixture())) {
            final byte[][] packets = {oldSms, oldCall};
            final List<byte[]> replies = new ArrayList<>();
            RadioEventSessionPump.FrameTransport transport = new RadioEventSessionPump.FrameTransport() {
                int position;
                public byte[] read() { return position < packets.length ? packets[position++] : null; }
                public void write(byte[] packet) { replies.add(packet.clone()); }
            };
            RadioEventSessionPump.Result result = RadioServiceMain.runEventTransport(first.endpoint, transport, oldEpoch);
            check(result.isClean(), "EOF did not clean the old session");
            check(replies.size() == 3 && replies.get(1)[23] == 1 && replies.get(2)[23] == 1,
                    "SMS or call did not receive submission ACK");
            check(first.smsEvents.get() == 1 && first.rings.get() == 1 && first.states.get() == 2,
                    "disconnect did not notify call cleanup");
        }
        // A fresh service instance has new messaging state and a distinct authenticated epoch.
        try (RadioEventSharedStateTest.Fixture restarted = new RadioEventSharedStateTest.Fixture(
                new RadioEventSharedStateTest.ReadyFixture())) {
            RadioEventSessionManager manager = new RadioEventSessionManager(restarted.endpoint);
            RadioEventSession session = manager.openOnce(newEpoch);
            check(session.receive(oldSms) == null && session.receive(oldCall) == null,
                    "a frame from the disconnected session was accepted");
            check(restarted.smsEvents.get() == 0 && restarted.rings.get() == 0,
                    "stale frames caused callbacks after restart");
            byte[] smsAck = session.receive(sms(newEpoch, 1));
            byte[] callAck = session.receive(call(newEpoch, 2));
            check(smsAck != null && smsAck[23] == 1 && callAck != null && callAck[23] == 1,
                    "fresh service did not accept its first SMS and call");
            check(restarted.smsEvents.get() == 1 && restarted.rings.get() == 1,
                    "fresh events did not reach callbacks");
            check(session.closeSession() == RadioEventSession.CloseResult.CLEANED && restarted.states.get() == 2,
                    "fresh session failed call cleanup");
            check(session.receive(sms(newEpoch, 3)) == null, "closed session accepted an event");
            try { manager.openOnce(newEpoch); throw new AssertionError("session reopened in same process"); }
            catch (IllegalStateException expected) { }
        }
        System.out.println("RADIO_EVENT_DISCONNECT_RESTART_OK");
    }
}
