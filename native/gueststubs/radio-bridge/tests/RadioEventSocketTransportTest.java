package app.aemu.stub;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

public final class RadioEventSocketTransportTest {
    private static void check(boolean ok) { if (!ok) throw new AssertionError(); }
    private static byte[] ints(int... values) {
        ByteBuffer b=ByteBuffer.allocate(values.length*4).order(ByteOrder.nativeOrder());
        for (int v:values) b.putInt(v);
        return b.array();
    }
    public static void main(String[] args) throws Exception {
        List<Integer> closed=new ArrayList<>();
        RadioEventSocketTransport.Packet good=new RadioEventSocketTransport.Packet(new byte[]{1},0,
                new int[]{1},new int[]{2},new byte[][]{ints(100,200,300)});
        RadioEventSocketTransport t=new RadioEventSocketTransport(100,200,() -> good, closed::add);
        check(Arrays.equals(t.read(),new byte[]{1}) && !t.requiresProcessExit());
        RadioEventSocketTransport.Packet hostile=new RadioEventSocketTransport.Packet(new byte[]{1},0,
                new int[]{1,1},new int[]{2,1},new byte[][]{ints(100,200,300),ints(7,8)});
        t=new RadioEventSocketTransport(100,200,() -> hostile, closed::add);
        try { t.read(); throw new AssertionError("rights were accepted"); } catch(IOException expected) { }
        check(closed.equals(Arrays.asList(7,8)) && t.requiresProcessExit());
        RadioEventSocketTransport.Packet other=new RadioEventSocketTransport.Packet(new byte[]{1},0,
                new int[]{1},new int[]{2},new byte[][]{ints(101,200,300)});
        t=new RadioEventSocketTransport(100,200,() -> other, closed::add);
        try { t.read(); throw new AssertionError("wrong PID accepted"); } catch(IOException expected) { }
        check(t.requiresProcessExit());
        t=new RadioEventSocketTransport(100,200,() -> { throw new IOException("recv failed"); }, closed::add);
        try { t.read(); throw new AssertionError("read failure ignored"); } catch(IOException expected) { }
        check(t.requiresProcessExit());
        try { t.read(); throw new AssertionError("terminal transport reused"); } catch(IOException expected) { }
        RadioEventSocketTransport.Packet truncated=new RadioEventSocketTransport.Packet(new byte[]{1},8,
                new int[]{1,1},new int[]{2,1},new byte[][]{ints(100,200,300),ints(9,10)});
        closed.clear();
        t=new RadioEventSocketTransport(100,200,() -> truncated, closed::add);
        try { t.read(); throw new AssertionError("truncated control accepted"); } catch(IOException expected) { }
        check(closed.equals(Arrays.asList(9,10)) && t.requiresProcessExit());
        closed.clear();
        t=new RadioEventSocketTransport(100,200,() -> hostile, fd -> {
            closed.add(fd); if(fd==7) throw new IOException("close failure");
        });
        try { t.read(); throw new AssertionError("close failure ignored"); } catch(IOException expected) { }
        check(closed.equals(Arrays.asList(7,8)) && t.requiresProcessExit());
        t=new RadioEventSocketTransport(100,200,() -> { throw new OutOfMemoryError("synthetic conversion failure"); }, closed::add);
        try { t.read(); throw new AssertionError("conversion failure ignored"); } catch(OutOfMemoryError expected) { }
        check(t.requiresProcessExit());
        t=new RadioEventSocketTransport(100,200,() -> null, closed::add);
        check(t.read()==null && !t.requiresProcessExit());
        t=new RadioEventSocketTransport(100,200,() -> good, closed::add, frame -> { throw new IOException("write failure"); });
        try { t.write(new byte[]{1}); throw new AssertionError("write failure ignored"); } catch(IOException expected) { }
        check(t.requiresProcessExit());
        System.out.println("RADIO_EVENT_SOCKET_TRANSPORT_OK");
    }
}
