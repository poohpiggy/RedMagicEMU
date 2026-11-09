package app.aemu.stub;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;

/**
 * Validates one frame's kernel-reported SCM_CREDENTIALS metadata only.
 *
 * The caller must obtain the flags and control messages directly from recvmsg;
 * this class does not establish their provenance or manage received file descriptors.
 * In particular, callers must close any received SCM_RIGHTS descriptors even when
 * this validation rejects a frame. SO_PEERCRED cannot replace these credentials:
 * its UID can be rewritten in the Guest environment.
 */
final class RadioGuestPeerCredentials {
    private static final int SOL_SOCKET = 1;
    private static final int SCM_CREDENTIALS = 2;
    private static final int MSG_CTRUNC = 0x08;
    private static final int MSG_TRUNC = 0x20;
    private static final int UCRED_SIZE = 12;

    private RadioGuestPeerCredentials() {}

    static boolean matches(int expectedPid, int expectedUid, int flags,
            int[] levels, int[] types, byte[][] data) {
        if (expectedPid <= 0 || expectedUid < 0 || (flags & (MSG_TRUNC | MSG_CTRUNC)) != 0
                || levels == null || types == null || data == null
                || levels.length != 1 || types.length != 1 || data.length != 1
                || levels[0] != SOL_SOCKET || types[0] != SCM_CREDENTIALS
                || data[0] == null || data[0].length != UCRED_SIZE) {
            return false;
        }
        ByteBuffer credentials = ByteBuffer.wrap(data[0]).order(ByteOrder.nativeOrder());
        return credentials.getInt() == expectedPid && credentials.getInt() == expectedUid;
    }
}
