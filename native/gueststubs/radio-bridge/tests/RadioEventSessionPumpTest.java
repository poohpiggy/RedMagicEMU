package app.aemu.stub;

import android.hardware.radio.messaging.IRadioMessagingIndication;
import android.hardware.radio.messaging.IRadioMessagingResponse;
import android.hardware.radio.voice.IRadioVoiceIndication;
import android.hardware.radio.voice.IRadioVoiceResponse;
import android.os.IBinder;
import android.os.RemoteException;
import java.io.IOException;
import java.lang.reflect.Field;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

/** In-memory framed transport only: this does not open a socket or register a service. */
public final class RadioEventSessionPumpTest {
    private static void check(boolean ok, String message) { if (!ok) throw new AssertionError(message); }
    private static IBinder peer() {
        return (IBinder)Proxy.newProxyInstance(IBinder.class.getClassLoader(),new Class<?>[]{IBinder.class},(p,m,a)->{
            switch(m.getName()) {
                case "linkToDeath": return null;
                case "unlinkToDeath": return true;
                case "isBinderAlive": return true;
                case "hashCode": return System.identityHashCode(p);
                case "equals": return p==a[0];
                default: throw new UnsupportedOperationException(m.getName());
            }
        });
    }
    private static final class Fixture implements AutoCloseable {
        final RadioVoiceService voice=new RadioVoiceService("slot1",8,3000);
        final RadioMessagingService sms=RadioMessagingService.withoutStateForTests("slot1");
        final AtomicInteger states=new AtomicInteger();
        boolean failRemoval;
        volatile CountDownLatch removalEntered, releaseRemoval;
        final RadioEventSessionManager manager;
        Fixture() throws Exception {
            IBinder vr=peer(),vi=peer(),sr=peer(),si=peer();
            IRadioVoiceResponse response=(IRadioVoiceResponse)Proxy.newProxyInstance(IRadioVoiceResponse.class.getClassLoader(),new Class<?>[]{IRadioVoiceResponse.class},
                (p,m,a)->m.getName().equals("asBinder") ? vr : null);
            IRadioVoiceIndication indication=(IRadioVoiceIndication)Proxy.newProxyInstance(IRadioVoiceIndication.class.getClassLoader(),new Class<?>[]{IRadioVoiceIndication.class},(p,m,a)->{
                if(m.getName().equals("asBinder")) return vi;
                if(m.getName().equals("callStateChanged") && states.incrementAndGet()>1) {
                    CountDownLatch entered=removalEntered,release=releaseRemoval;
                    if(entered!=null) entered.countDown();
                    if(release!=null && !release.await(5,TimeUnit.SECONDS)) throw new RemoteException("test removal blocked");
                    if(failRemoval) throw new RemoteException("test removal notification failed");
                }
                return null;
            });
            IRadioMessagingResponse smsResponse=(IRadioMessagingResponse)Proxy.newProxyInstance(IRadioMessagingResponse.class.getClassLoader(),new Class<?>[]{IRadioMessagingResponse.class},
                (p,m,a)->m.getName().equals("asBinder") ? sr : null);
            IRadioMessagingIndication smsIndication=(IRadioMessagingIndication)Proxy.newProxyInstance(IRadioMessagingIndication.class.getClassLoader(),new Class<?>[]{IRadioMessagingIndication.class},
                (p,m,a)->m.getName().equals("asBinder") ? si : null);
            voice.setResponseFunctions(response,indication); sms.setResponseFunctions(smsResponse,smsIndication);
            manager=new RadioEventSessionManager(RadioEventEndpoint.withoutStateForTests(voice,sms));
        }
        @Override public void close() { voice.close(); }
    }
    private static void put64(byte[] b,int offset,long n) { for(int i=7;i>=0;i--){b[offset+i]=(byte)n;n>>>=8;} }
    private static byte[] incoming(long epoch) {
        byte[] b=new byte[24]; b[0]='R';b[1]='M';b[2]='E';b[3]='1';
        put64(b,4,epoch);put64(b,12,1);b[20]=2;b[22]=1;b[23]='1';return b;
    }
    private static byte[] expectedAck(long epoch) {
        byte[] b=incoming(epoch);b[20]=3;b[23]=1;return b;
    }
    private static Field field(Object value,String name) throws Exception {
        Field field=value.getClass().getDeclaredField(name);field.setAccessible(true);return field;
    }
    private static Object invoke(Method method,Object... args) throws Exception {
        try { return method.invoke(null,args); }
        catch (InvocationTargetException wrapped) {
            Throwable cause=wrapped.getCause();
            if(cause instanceof Exception) throw (Exception)cause;
            if(cause instanceof Error) throw (Error)cause;
            throw new AssertionError(cause);
        }
    }
    private static boolean isClean(Object result) throws Exception {
        Method method=result.getClass().getDeclaredMethod("isClean");method.setAccessible(true);
        return (Boolean)method.invoke(result);
    }
    private static void checkCase(Method run,Class<?> transportType,String mode) throws Exception {
        try(Fixture f=new Fixture()) {
            RadioEventSession session=f.manager.open();long epoch=session.epoch();
            IOException expected=new IOException("synthetic "+mode+" failure");
            List<byte[]> writes=new ArrayList<>();final int[] reads={0};
            Object transport=Proxy.newProxyInstance(transportType.getClassLoader(),new Class<?>[]{transportType},(p,m,a)->{
                if(m.getName().equals("read")) {
                    if(reads[0]++==0) return incoming(epoch);
                    if(mode.startsWith("read")) throw expected;
                    return null;
                }
                if(m.getName().equals("write")) {
                    if(mode.equals("write")) throw expected;
                    writes.add(((byte[])a[0]).clone());return null;
                }
                throw new AssertionError("unexpected transport method "+m.getName());
            });
            if(mode.endsWith("cleanup")) f.failRemoval=true;
            Object result=invoke(run,session,transport);
            Object close=field(result,"closeResult").get(result);
            Object failure=field(result,"transportFailure").get(result);
            check(f.states.get()==2,"EOF or error failed to notify call removal");
            if(mode.endsWith("cleanup")) {
                check("UNCERTAIN".equals(close.toString()) && (mode.startsWith("read") ? failure==expected : failure==null) && !isClean(result) &&
                    field(result,"closeFailure").get(result)!=null && f.manager.mustExitProcess(),"cleanup failure was hidden");
                try { f.manager.open();throw new AssertionError("session opened after uncertain cleanup"); }
                catch(IllegalStateException expectedFailure) { }
            } else {
                check("CLEANED".equals(close.toString()) && !f.manager.mustExitProcess(),"transport termination was not cleaned");
                check((mode.equals("read") || mode.equals("write")) ? failure==expected : failure==null,"transport failure lost or invented");
                check(isClean(result)==mode.equals("eof"),"transport failure treated as successful pump");
                if(!mode.equals("write")) check(writes.size()==1 && Arrays.equals(writes.get(0),expectedAck(epoch)),"incorrect ACK");
                RadioEventSession next=f.manager.open();
                check(Arrays.equals(next.receive(incoming(next.epoch())),expectedAck(next.epoch())),"new call blocked by old session");
                check(next.closeSession()==RadioEventSession.CloseResult.CLEANED,"new session cleanup failed");
            }
        }
    }
    private static void uncheckedFailureStillCloses(Method run,Class<?> transportType,boolean failCleanup) throws Exception {
        try(Fixture f=new Fixture()) {
            RadioEventSession session=f.manager.open();long epoch=session.epoch();
            check(Arrays.equals(session.receive(incoming(epoch)),expectedAck(epoch)),"initial call was not accepted");
            f.failRemoval=failCleanup;
            AssertionError expected=new AssertionError("synthetic fatal transport error");
            Object transport=Proxy.newProxyInstance(transportType.getClassLoader(),new Class<?>[]{transportType},(p,m,a)->{
                if(m.getName().equals("read")) throw expected;
                throw new AssertionError("unexpected transport method");
            });
            try { invoke(run,session,transport); throw new AssertionError("fatal transport error swallowed"); }
            catch(AssertionError actual) { check(actual==expected,"wrong fatal transport error"); }
            check(f.states.get()==2 && f.manager.mustExitProcess()==failCleanup,"fatal transport error skipped cleanup or hid uncertainty");
            if(failCleanup) {
                check(session.closeSession()==RadioEventSession.CloseResult.UNCERTAIN && session.closeFailure()!=null,
                    "fatal transport error hid cleanup failure");
                return;
            }
            RadioEventSession next=f.manager.open();
            check(Arrays.equals(next.receive(incoming(next.epoch())),expectedAck(next.epoch())),"old call survived fatal transport error");
            check(next.closeSession()==RadioEventSession.CloseResult.CLEANED,"next session failed cleanup");
        }
    }
    private static void inProgressIsNotClean(Method run,Class<?> transportType) throws Exception {
        CountDownLatch entered=new CountDownLatch(1),release=new CountDownLatch(1);
        try(Fixture f=new Fixture()) {
            RadioEventSession session=f.manager.open();long epoch=session.epoch();
            check(Arrays.equals(session.receive(incoming(epoch)),expectedAck(epoch)),"initial call was not accepted");
            f.removalEntered=entered;f.releaseRemoval=release;
            AtomicReference<RadioEventSession.CloseResult> first=new AtomicReference<>();
            AtomicReference<Throwable> error=new AtomicReference<>();
            Thread closer=new Thread(()->{try{first.set(session.closeSession());}catch(Throwable failure){error.set(failure);}});
            Object transport=Proxy.newProxyInstance(transportType.getClassLoader(),new Class<?>[]{transportType},(p,m,a)->{
                if(m.getName().equals("read")) return null;
                throw new AssertionError("unexpected transport method");
            });
            try {
                closer.start();check(entered.await(2,TimeUnit.SECONDS),"cleanup did not enter callback");
                Object result=invoke(run,session,transport);
                check("IN_PROGRESS".equals(field(result,"closeResult").get(result).toString()) && !isClean(result),"in-progress cleanup falsely reported clean");
                try { f.manager.open();throw new AssertionError("new session opened before cleanup finished"); }
                catch(IllegalStateException expected) { }
            } finally { release.countDown();closer.join(3000); }
            check(!closer.isAlive() && error.get()==null && first.get()==RadioEventSession.CloseResult.CLEANED,"first cleanup failed");
        } finally { release.countDown(); }
    }
    private static void invalidFrameStillCloses(Method run,Class<?> transportType) throws Exception {
        try(Fixture f=new Fixture()) {
            RadioEventSession session=f.manager.open();long epoch=session.epoch();
            check(Arrays.equals(session.receive(incoming(epoch)),expectedAck(epoch)),"initial call was not accepted");
            Object transport=Proxy.newProxyInstance(transportType.getClassLoader(),new Class<?>[]{transportType},(p,m,a)->{
                if(m.getName().equals("read")) return new byte[]{1};
                throw new AssertionError("invalid frame produced a reply");
            });
            Object result=invoke(run,session,transport);
            check(field(result,"transportFailure").get(result) instanceof IOException && !isClean(result) &&
                "CLEANED".equals(field(result,"closeResult").get(result).toString()),"invalid frame did not retire and clean the session");
            check(f.states.get()==2 && !f.manager.mustExitProcess(),"invalid frame left its call active");
        }
    }
    public static void main(String[] args) throws Exception {
        final Class<?> pump,transport;
        try { pump=Class.forName("app.aemu.stub.RadioEventSessionPump");transport=Class.forName("app.aemu.stub.RadioEventSessionPump$FrameTransport"); }
        catch(ClassNotFoundException missing) { throw new AssertionError("event session transport pump missing",missing); }
        Method run=pump.getDeclaredMethod("run",RadioEventSession.class,transport);run.setAccessible(true);
        checkCase(run,transport,"eof");checkCase(run,transport,"read");checkCase(run,transport,"write");
        checkCase(run,transport,"cleanup");checkCase(run,transport,"read_cleanup");
        uncheckedFailureStillCloses(run,transport,false);uncheckedFailureStillCloses(run,transport,true);
        inProgressIsNotClean(run,transport);invalidFrameStillCloses(run,transport);
        System.out.println("RADIO_EVENT_SESSION_PUMP_OK");
    }
}
