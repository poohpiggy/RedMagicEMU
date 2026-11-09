package app.aemu.stub;

import java.lang.reflect.Method;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;

/** Pure metadata checks only; no real socket, ancillary FD ownership or kernel provenance. */
public final class RadioGuestPeerCredentialsTest {
    private static void check(boolean value, String message) {
        if (!value) throw new AssertionError(message);
    }

    private static byte[] credential(int pid, int uid) {
        return ByteBuffer.allocate(12).order(ByteOrder.nativeOrder())
                .putInt(pid).putInt(uid).putInt(4321).array();
    }

    public static void main(String[] args) throws Exception {
        Class<?> type;
        try { type = Class.forName("app.aemu.stub.RadioGuestPeerCredentials"); }
        catch (ClassNotFoundException missing) { throw new AssertionError("guest per-frame credential validator missing", missing); }
        Method accept = type.getDeclaredMethod("matches", int.class, int.class, int.class,
                int[].class, int[].class, byte[][].class);
        accept.setAccessible(true);
        byte[] valid = credential(1200, 10042);
        java.util.function.Function<Object[], Boolean> test = values -> {
            try { return (Boolean) accept.invoke(null, values); }
            catch (ReflectiveOperationException error) { throw new AssertionError(error); }
        };
        check(test.apply(new Object[]{1200, 10042, 0, new int[]{1}, new int[]{2}, new byte[][]{valid}}), "matching per-frame credentials rejected");
        check(!test.apply(new Object[]{1200, 10042, 0, new int[]{}, new int[]{}, new byte[][]{}}), "missing credentials accepted");
        check(!test.apply(new Object[]{1200, 10042, 0, new int[]{1,1}, new int[]{2,2}, new byte[][]{valid,valid}}), "duplicate credentials accepted");
        check(!test.apply(new Object[]{1200, 10042, 0x20, new int[]{1}, new int[]{2}, new byte[][]{valid}}), "truncated data accepted");
        check(!test.apply(new Object[]{1200, 10042, 0x08, new int[]{1}, new int[]{2}, new byte[][]{valid}}), "truncated control accepted");
        check(!test.apply(new Object[]{1200, 10042, 0, new int[]{1}, new int[]{2}, new byte[][]{credential(1201,10042)}}), "same-UID other PID accepted");
        check(!test.apply(new Object[]{1200, 10042, 0, new int[]{1}, new int[]{2}, new byte[][]{credential(1200,10043)}}), "different UID accepted");
        check(!test.apply(new Object[]{1200, 10042, 0, new int[]{1}, new int[]{1}, new byte[][]{valid}}), "unexpected ancillary type accepted");
        check(!test.apply(new Object[]{1200, 10042, 0, new int[]{2}, new int[]{2}, new byte[][]{valid}}), "wrong level accepted");
        check(!test.apply(new Object[]{1200, 10042, 0, new int[]{1}, new int[]{2}, new byte[][]{new byte[11]}}), "malformed credentials accepted");
        check(!test.apply(new Object[]{1200, 10042, 0, new int[]{1}, new int[]{2}, new byte[][]{new byte[13]}}), "oversized credentials accepted");
        check(!test.apply(new Object[]{1200, 10042, 0, new int[]{1}, new int[]{2}, new byte[][]{null}}), "null credential accepted");
        check(!test.apply(new Object[]{0, 10042, 0, new int[]{1}, new int[]{2}, new byte[][]{valid}}), "invalid expected PID accepted");
        check(!test.apply(new Object[]{1200, -1, 0, new int[]{1}, new int[]{2}, new byte[][]{valid}}), "invalid expected UID accepted");
        check(!test.apply(new Object[]{1200, 10042, 0, null, new int[]{2}, new byte[][]{valid}}), "missing metadata accepted");
        check(!test.apply(new Object[]{1200, 10042, 0, new int[]{1}, new int[]{}, new byte[][]{valid}}), "unequal metadata lengths accepted");
        System.out.println("RADIO_GUEST_PEER_CREDENTIALS_OK");
    }
}
