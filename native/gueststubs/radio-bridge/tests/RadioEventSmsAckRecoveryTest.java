package app.aemu.stub;

/** Real shared slot/session with synthetic callbacks; this is not an Android IPC test. */
public final class RadioEventSmsAckRecoveryTest {
    private static void check(boolean ok, String message) { if (!ok) throw new AssertionError(message); }

    private static void expect(RadioEventSession session, long epoch, int sequence, int status) {
        byte[] packet = RadioEventSharedStateTest.frame(epoch, sequence, 1, new byte[]{0, 4, 11});
        byte[] reply = session.receive(packet);
        check(reply != null && reply[23] == status, "unexpected SMS status at " + sequence + ": " +
                (reply == null ? "null" : reply[23]));
    }

    public static void main(String[] args) throws Exception {
        long epoch = 91;
        try (RadioEventSharedStateTest.Fixture f = new RadioEventSharedStateTest.Fixture(
                new RadioEventSharedStateTest.ReadyFixture())) {
            RadioEventSession session = new RadioEventSession(epoch, f.endpoint);
            expect(session, epoch, 1, 1);
            check(f.smsEvents.get() == 1, "first SMS did not reach callback");
            f.slot.setRadioPower(false);
            expect(session, epoch, 2, 2);
            f.slot.setRadioPower(true);
            expect(session, epoch, 3, 2);
            check(f.smsEvents.get() == 1, "pending SMS caused duplicate indication");
            f.sms.acknowledgeLastIncomingGsmSms(100, true, 0);
            expect(session, epoch, 4, 1);
            check(f.smsEvents.get() == 2, "SMS did not resume after late ACK");
        }
        try (RadioEventSharedStateTest.Fixture f = new RadioEventSharedStateTest.Fixture(
                new RadioEventSharedStateTest.ReadyFixture())) {
            f.failSmsWithIllegalState = true;
            RadioEventSession session = new RadioEventSession(epoch, f.endpoint);
            expect(session, epoch, 1, 3);
            check(f.smsEvents.get() == 1, "callback failure did not occur after invocation");
        }
        System.out.println("RADIO_EVENT_SMS_ACK_RECOVERY_OK");
    }
}
