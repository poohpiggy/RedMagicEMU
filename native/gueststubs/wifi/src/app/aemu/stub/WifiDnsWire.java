package app.aemu.stub;
import java.nio.ByteBuffer;
final class WifiDnsWire {
    private static final byte[] MAGIC = {82, 77, 69, 49};
    static class Request {
        final int operation; final long token, netId, sequence;
        Request(int operation, long token, long netId, long sequence) {
            this.operation=operation; this.token=token; this.netId=netId; this.sequence=sequence;
        }
    }
    private final long epoch;
    private long sequence;
    WifiDnsWire(long epoch) { if (epoch <= 0) throw new IllegalArgumentException(); this.epoch = epoch; }
    synchronized Request read(byte[] bytes) {
        if (bytes == null || bytes.length != 36 || sequence == Long.MAX_VALUE-1) return null;
        ByteBuffer b = ByteBuffer.wrap(bytes);
        for (byte expected : MAGIC) if (b.get() != expected) return null;
        long actualEpoch = b.getLong(), actualSeq = b.getLong();
        int kind = Byte.toUnsignedInt(b.get()), len = Short.toUnsignedInt(b.getShort());
        int op = Byte.toUnsignedInt(b.get()); long token = b.getLong(), netId = Integer.toUnsignedLong(b.getInt());
        if (actualEpoch != epoch || actualSeq != sequence + 1 || kind != 1 || len != 13 ||
                (op != 1 && op != 2 && op != 3) || token <= 0 || (op == 2 && netId != 0) ||
                (op == 3 && netId > 1)) return null;
        sequence = actualSeq;
        return new Request(op, token, netId, actualSeq);
    }
    static byte[] ack(long epoch, long seq, boolean valid) {
        if (epoch <= 0 || seq <= 0) throw new IllegalArgumentException();
        return ByteBuffer.allocate(24).put(MAGIC).putLong(epoch).putLong(seq)
                .put((byte)3).putShort((short)1).put((byte)(valid ? 1 : 2)).array();
    }
}
