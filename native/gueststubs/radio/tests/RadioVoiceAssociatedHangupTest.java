package app.aemu.stub;

import android.hardware.radio.voice.Call;
import android.hardware.radio.voice.IRadioVoiceIndication;
import android.hardware.radio.voice.IRadioVoiceResponse;
import android.os.IBinder;
import android.os.RemoteException;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

/** Local callbacks and synthetic identities only; this does not register a Radio service. */
public final class RadioVoiceAssociatedHangupTest {
    private static void check(boolean value, String reason) { if (!value) throw new AssertionError(reason); }
    private static final class Peer {
        final List<IBinder.DeathRecipient> deaths = new ArrayList<>();
        final IBinder binder = (IBinder) Proxy.newProxyInstance(IBinder.class.getClassLoader(),new Class<?>[]{IBinder.class},(p,m,a)-> {
            switch (m.getName()) {
                case "linkToDeath": deaths.add((IBinder.DeathRecipient)a[0]); return null;
                case "unlinkToDeath": return deaths.remove(a[0]);
                case "isBinderAlive": return true;
                case "hashCode": return System.identityHashCode(p);
                case "equals": return p==a[0];
                default: throw new UnsupportedOperationException(m.getName());
            }
        });
        void die() { for (IBinder.DeathRecipient recipient : new ArrayList<>(deaths)) recipient.binderDied(); }
    }
    private static final class Response {
        final Peer peer=new Peer(); volatile Call[] calls;
        final IRadioVoiceResponse proxy=(IRadioVoiceResponse) Proxy.newProxyInstance(IRadioVoiceResponse.class.getClassLoader(),new Class<?>[]{IRadioVoiceResponse.class},(p,m,a)-> {
            if(m.getName().equals("asBinder")) return peer.binder;
            if(m.getName().equals("getCurrentCallsResponse")) calls=(Call[])a[1];
            return null;
        });
    }
    private static final class Indication {
        final Peer peer=new Peer(); final AtomicInteger states=new AtomicInteger();
        volatile boolean failState;
        volatile CountDownLatch entered, unblock;
        final IRadioVoiceIndication proxy=(IRadioVoiceIndication) Proxy.newProxyInstance(IRadioVoiceIndication.class.getClassLoader(),new Class<?>[]{IRadioVoiceIndication.class},(p,m,a)-> {
            if(m.getName().equals("asBinder")) return peer.binder;
            if(m.getName().equals("callStateChanged")) {
                states.incrementAndGet();
                CountDownLatch signal=entered, wait=unblock;
                if(signal!=null) signal.countDown();
                if(wait!=null && !wait.await(3,TimeUnit.SECONDS)) throw new RemoteException("test indication timed out");
                if(failState) throw new RemoteException("test indication failed");
            }
            return null;
        });
    }
    private static Method incoming,hangup;
    private static boolean invoke(Method method,RadioVoiceService service,Object... args) throws Exception {
        try { return (Boolean) method.invoke(service,args); }
        catch(InvocationTargetException error) {
            Throwable cause=error.getCause();
            if(cause instanceof Exception) throw (Exception)cause;
            if(cause instanceof Error) throw (Error)cause;
            throw new AssertionError(cause);
        }
    }
    private static boolean call(RadioVoiceService service,String number,long epoch,long sequence) throws Exception {
        return invoke(incoming,service,number,epoch,sequence);
    }
    private static boolean end(RadioVoiceService service,long epoch,long sequence) throws Exception {
        return invoke(hangup,service,epoch,sequence);
    }
    private static void calls(RadioVoiceService service,Response response,int count,String number) throws Exception {
        service.getCurrentCalls(911);
        check(response.calls!=null && response.calls.length==count,"wrong number of current calls");
        if(count==1) check(number.equals(response.calls[0].number),"wrong current call");
    }
    private static final class ReadySlot extends SyntheticSingleSlot {
        @Override synchronized boolean isNetworkRegistered() { return true; }
        @Override synchronized boolean isSimReady() { return true; }
    }
    private static void identityAndGuestLifecycle() throws Exception {
        RadioVoiceService voice=new RadioVoiceService("slot1",8,3000); Response response=new Response(); Indication indication=new Indication();
        try {
            voice.setResponseFunctions(response.proxy,indication.proxy);
            check(!call(voice,"10086",0,1) && !call(voice,"10086",1,0),"zero identity accepted");
            check(!end(voice,0,1) && !end(voice,1,0),"zero hangup identity accepted");
            check(call(voice,"10086",5,1),"call A rejected");
            check(!end(voice,6,1) && !end(voice,5,2),"wrong identity ended call A");
            voice.rejectCall(1);
            check(call(voice,"10086",5,2),"same-number call B rejected");
            check(!end(voice,5,1),"delayed hangup of A ended call B");
            calls(voice,response,1,"10086");
            voice.acceptCall(2);
            check(end(voice,5,2),"matching active call could not be ended");
            calls(voice,response,0,null);
            check(!end(voice,5,2),"duplicate hangup accepted");
            check(call(voice,"10086",6,1),"new epoch call rejected");
            check(!end(voice,5,1),"old epoch ended new call");
            voice.hangup(3,1);
            check(call(voice,"10086",6,2),"call after Guest hangup rejected");
            check(!end(voice,6,1),"old Guest-hung-up identity ended new call");
            check(end(voice,6,2),"matching ringing call could not be ended");
            check(voice.injectIncoming("10086"),"legacy incoming rejected");
            check(!end(voice,6,2),"legacy incoming matched an associated hangup");
            voice.rejectCall(4);
            check(call(voice,"10086",9,1),"call before callback death rejected");
            response.peer.die();
            Response rebound=new Response(); Indication reboundIndication=new Indication();
            voice.setResponseFunctions(rebound.proxy,reboundIndication.proxy);
            check(call(voice,"10086",9,2),"call after callback death rejected");
            check(!end(voice,9,1),"old identity survived callback death");
            calls(voice,rebound,1,"10086");
            check(end(voice,9,2),"new identity could not be ended after callback death");
        } finally { voice.close(); }
    }
    private static void interruptedCallAndPower() throws Exception {
        ReadySlot slot=new ReadySlot(); slot.setRadioPower(true);
        RadioVoiceService voice=new RadioVoiceService("slot1",8,3000,slot); Response response=new Response(); Indication indication=new Indication();
        try {
            voice.setResponseFunctions(response.proxy,indication.proxy);
            indication.failState=true;
            try { call(voice,"10086",7,1); throw new AssertionError("indication failure hidden"); }
            catch(RemoteException expected) { }
            indication.failState=false;
            calls(voice,response,1,"10086");
            check(end(voice,7,1),"partially indicated call lost its identity");
            check(call(voice,"10086",7,4),"call before failed hangup rejected");
            indication.failState=true;
            try { end(voice,7,4); throw new AssertionError("hangup indication failure hidden"); }
            catch(RemoteException expected) { }
            indication.failState=false;
            calls(voice,response,0,null);
            check(call(voice,"10086",7,5),"call after failed hangup rejected");
            check(!end(voice,7,4),"failed old hangup affected new call");
            check(end(voice,7,5),"new call after failed hangup could not be ended");
            check(call(voice,"10086",7,6),"call before power loss rejected");
            slot.setRadioPower(false); slot.setRadioPower(true);
            check(call(voice,"10086",7,7),"call after power cycle rejected");
            check(!end(voice,7,6),"old identity survived power cycle");
            check(end(voice,7,7),"new identity did not survive power cycle");
        } finally { voice.close(); }
    }
    private static void rebindDuringRemoval() throws Exception {
        RadioVoiceService voice=new RadioVoiceService("slot1",8,3000); Response oldResponse=new Response(); Indication oldIndication=new Indication();
        Response newResponse=new Response(); Indication newIndication=new Indication();
        CountDownLatch entered=new CountDownLatch(1),unblock=new CountDownLatch(1);
        AtomicReference<Throwable> failure=new AtomicReference<>();
        Thread worker=new Thread(() -> { try { end(voice,8,1); } catch(Throwable error) { failure.set(error); } });
        try {
            voice.setResponseFunctions(oldResponse.proxy,oldIndication.proxy);
            check(call(voice,"10086",8,1),"rebind test call rejected");
            oldIndication.entered=entered; oldIndication.unblock=unblock;
            worker.start(); check(entered.await(2,TimeUnit.SECONDS),"removal indication not entered");
            voice.setResponseFunctions(newResponse.proxy,newIndication.proxy);
            check(newIndication.states.get()>0,"new callback missed in-flight call removal");
            unblock.countDown(); worker.join(3000);
            check(!worker.isAlive(),"remote hangup did not finish");
            check(failure.get() instanceof RemoteException,"callback rebind did not report uncertain hangup");
            calls(voice,newResponse,0,null);
        } finally { unblock.countDown(); worker.join(3000); voice.close(); }
    }
    public static void main(String[] args) throws Exception {
        try {
            incoming=RadioVoiceService.class.getDeclaredMethod("injectIncoming",String.class,long.class,long.class);
            hangup=RadioVoiceService.class.getDeclaredMethod("injectRemoteHangup",long.class,long.class);
        } catch(NoSuchMethodException missing) { throw new AssertionError("associated incoming and remote hangup are not implemented",missing); }
        incoming.setAccessible(true); hangup.setAccessible(true);
        identityAndGuestLifecycle(); interruptedCallAndPower(); rebindDuringRemoval();
        System.out.println("RADIO_VOICE_ASSOCIATED_HANGUP_OK");
    }
}
