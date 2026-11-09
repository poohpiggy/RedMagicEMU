package app.aemu.stub;

import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/** Verifies the service-to-session wiring with synthetic callbacks and frames. */
public final class RadioEventServiceTest {
    private static void check(boolean value, String text) { if (!value) throw new AssertionError(text); }
    private static void put64(byte[] b,int at,long n) { for(int i=7;i>=0;--i){b[at+i]=(byte)n;n>>>=8;} }
    private static byte[] frame(long epoch,long seq,int kind,byte[] payload) {
        byte[] b=new byte[23+payload.length];b[0]='R';b[1]='M';b[2]='E';b[3]='1';
        put64(b,4,epoch);put64(b,12,seq);b[20]=(byte)kind;b[21]=(byte)(payload.length>>>8);b[22]=(byte)payload.length;
        System.arraycopy(payload,0,b,23,payload.length);return b;
    }
    private static final class Transport implements RadioEventSessionPump.FrameTransport {
        final byte[][] input; final List<byte[]> output=new ArrayList<>(); int index; boolean failWrite;
        Transport(byte[]... frames) { input=frames; }
        @Override public byte[] read() { return index<input.length ? input[index++] : null; }
        @Override public void write(byte[] bytes) throws IOException {
            if(failWrite) throw new IOException("test write failed");
            output.add(bytes.clone());
        }
    }
    public static void main(String[] args) throws Exception {
        long epoch=123456789;
        try (RadioEventSharedStateTest.Fixture f=new RadioEventSharedStateTest.Fixture(new RadioEventSharedStateTest.ReadyFixture())) {
            Transport t=new Transport(frame(epoch,1,1,new byte[]{0,4,11}),frame(epoch,2,2,new byte[]{'+','1','0','0','8','6'}));
            RadioEventSessionPump.Result result=RadioServiceMain.runEventTransport(f.endpoint,t,epoch);
            check(result.isClean(),"EOF did not clean event session");
            check(t.output.size()==3 && Arrays.equals(t.output.get(0),frame(epoch,Long.MAX_VALUE,3,new byte[]{1})),"incorrect hello");
            check(!Arrays.equals(t.output.get(0),t.output.get(1)),"hello must not match the first event ACK");
            check(Arrays.equals(t.output.get(1),frame(epoch,1,3,new byte[]{1})),"incorrect SMS acknowledgment");
            check(Arrays.equals(t.output.get(2),frame(epoch,2,3,new byte[]{1})),"incorrect call acknowledgment");
            check(f.smsEvents.get()==1 && f.rings.get()==1,"events did not reach shared models");
        }
        try (RadioEventSharedStateTest.Fixture f=new RadioEventSharedStateTest.Fixture(new RadioEventSharedStateTest.ReadyFixture())) {
            Transport t=new Transport();t.failWrite=true;
            try {RadioServiceMain.runEventTransport(f.endpoint,t,epoch);throw new AssertionError("hello failure accepted");}
            catch(IOException expected) { }
        }
        try (RadioEventSharedStateTest.Fixture f=new RadioEventSharedStateTest.Fixture(new RadioEventSharedStateTest.ReadyFixture())) {
            RadioEventSessionManager manager=new RadioEventSessionManager(f.endpoint);
            RadioEventSession session=manager.openOnce(epoch);
            check(session.closeSession()==RadioEventSession.CloseResult.CLEANED,"session did not close");
            try {manager.openOnce(epoch);throw new AssertionError("external epoch reused");} catch(IllegalStateException expected) { }
            try {manager.open();throw new AssertionError("session reopened after external epoch");} catch(IllegalStateException expected) { }
        }
        check(RadioServiceMain.validEventArguments("rme-test_1",123,456,epoch),"valid parameters rejected");
        check(!RadioServiceMain.validEventArguments("../bad",123,456,epoch),"invalid name accepted");
        check(!RadioServiceMain.validEventArguments("name",0,456,epoch),"invalid PID accepted");
        check(!RadioServiceMain.validEventArguments("name",123,-1,epoch),"invalid UID accepted");
        check(!RadioServiceMain.validEventArguments("name",123,456,0),"zero epoch accepted");
        System.out.println("RADIO_EVENT_SERVICE_OK");
    }
}
