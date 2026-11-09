package app.aemu.stub;

import android.hardware.radio.voice.Call;
import android.hardware.radio.voice.IRadioVoiceIndication;
import android.hardware.radio.voice.IRadioVoiceResponse;
import android.os.IBinder;
import android.os.RemoteException;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

/** Synthetic callbacks and identities; no Radio registration or device data. */
public final class RadioVoiceSessionCloseTest {
    private static void check(boolean ok,String message) { if (!ok) throw new AssertionError(message); }
    private static IBinder peer() {
        return (IBinder)Proxy.newProxyInstance(IBinder.class.getClassLoader(),new Class<?>[]{IBinder.class},(p,m,a)-> {
            switch (m.getName()) {
                case "linkToDeath": return null;
                case "unlinkToDeath": return true;
                case "isBinderAlive": return true;
                case "hashCode": return System.identityHashCode(p);
                case "equals": return p==a[0];
                default: throw new UnsupportedOperationException(m.getName());
            }
        });
    }
    private static final class Response {
        final IBinder binder=peer(); volatile Call[] calls;
        final IRadioVoiceResponse callback=(IRadioVoiceResponse)Proxy.newProxyInstance(IRadioVoiceResponse.class.getClassLoader(),new Class<?>[]{IRadioVoiceResponse.class},(p,m,a)-> {
            if (m.getName().equals("asBinder")) return binder;
            if (m.getName().equals("getCurrentCallsResponse")) calls=(Call[])a[1];
            return null;
        });
    }
    private static final class Indication {
        final IBinder binder=peer(); final AtomicInteger states=new AtomicInteger();
        volatile boolean fail; volatile CountDownLatch entered,release,removed;
        final IRadioVoiceIndication callback=(IRadioVoiceIndication)Proxy.newProxyInstance(IRadioVoiceIndication.class.getClassLoader(),new Class<?>[]{IRadioVoiceIndication.class},(p,m,a)-> {
            if (m.getName().equals("asBinder")) return binder;
            if (m.getName().equals("callStateChanged")) {
                int stateCount=states.incrementAndGet(); CountDownLatch signal=entered,wait=release;
                if (signal!=null) signal.countDown();
                if (wait!=null && !wait.await(3,TimeUnit.SECONDS)) throw new RemoteException("test timed out");
                if (fail) throw new RemoteException("synthetic callback failure");
                if (stateCount==2 && removed!=null) removed.countDown();
            }
            return null;
        });
    }
    private static Method closeSession,claimOwner,allowsUnmanaged,claimEndpoint;
    private static boolean close(RadioVoiceService voice,long epoch) throws Exception {
        try { return (Boolean)closeSession.invoke(voice,epoch); }
        catch (InvocationTargetException error) {
            Throwable cause=error.getCause();
            if (cause instanceof Exception) throw (Exception)cause;
            if (cause instanceof Error) throw (Error)cause;
            throw new AssertionError(cause);
        }
    }
    private static void count(RadioVoiceService voice,Response response,int count) throws Exception {
        voice.getCurrentCalls(901);
        check(response.calls!=null && response.calls.length==count,"incorrect number of calls");
    }
    private static void identityAndLifecycle() throws Exception {
        RadioVoiceService voice=new RadioVoiceService("slot1",8,3000);
        Response response=new Response(); Indication indication=new Indication();
        try {
            voice.setResponseFunctions(response.callback,indication.callback);
            check(!close(voice,0),"zero epoch accepted");
            check(voice.injectIncoming("10086",11,1),"call A rejected");
            check(!close(voice,12),"other epoch ended call A"); count(voice,response,1);
            check(close(voice,11),"matching epoch not cleared"); count(voice,response,0);
            check(!close(voice,11),"repeat close removed a call");
            check(voice.injectIncoming("10086",12,1),"call B rejected");
            check(!close(voice,11),"late close of A ended B"); count(voice,response,1);
            voice.acceptCall(902);
            check(close(voice,12),"matching active call not cleared"); count(voice,response,0);
            check(voice.injectIncoming("10086"),"legacy call rejected");
            check(!close(voice,12),"session close cleared an unassociated call"); count(voice,response,1);
            voice.rejectCall(903);
        } finally { voice.close(); }
    }
    private static void failedIndicationDoesNotRollback() throws Exception {
        RadioVoiceService voice=new RadioVoiceService("slot1",8,3000);
        Response response=new Response(); Indication indication=new Indication();
        try {
            voice.setResponseFunctions(response.callback,indication.callback);
            check(voice.injectIncoming("10086",21,1),"test call rejected");
            indication.fail=true;
            try { close(voice,21); throw new AssertionError("callback error hidden"); }
            catch (RemoteException expected) { }
            indication.fail=false; count(voice,response,0);
            check(voice.injectIncoming("10086",22,1),"new call rejected after failure");
            check(!close(voice,21),"late failed close removed new epoch"); count(voice,response,1);
            check(close(voice,22),"new epoch not cleared");
        } finally { voice.close(); }
    }
    private static void rebindDuringNotification() throws Exception {
        RadioVoiceService voice=new RadioVoiceService("slot1",8,3000);
        Response oldResponse=new Response(),newResponse=new Response();
        Indication oldIndication=new Indication(),newIndication=new Indication();
        CountDownLatch entered=new CountDownLatch(1),release=new CountDownLatch(1);
        AtomicReference<Throwable> failure=new AtomicReference<>();
        Thread worker=new Thread(() -> { try { close(voice,31); } catch (Throwable error) { failure.set(error); } });
        try {
            voice.setResponseFunctions(oldResponse.callback,oldIndication.callback);
            check(voice.injectIncoming("10086",31,1),"call rejected");
            oldIndication.entered=entered; oldIndication.release=release;
            worker.start(); check(entered.await(2,TimeUnit.SECONDS),"close notification not entered");
            voice.setResponseFunctions(newResponse.callback,newIndication.callback);
            check(newIndication.states.get()>0,"new callback missed removal replay");
            release.countDown(); worker.join(3000);
            check(!worker.isAlive() && failure.get() instanceof RemoteException,"rebind should make close uncertain");
            count(voice,newResponse,0);
        } finally { release.countDown(); worker.join(3000); voice.close(); }
    }
    private static void ownerClaimIsExclusive() throws Exception {
        RadioVoiceService voice=new RadioVoiceService("slot1",8,3000);
        try {
            check((Boolean)claimEndpoint.invoke(voice),"first endpoint claim failed");
            check(!(Boolean)claimEndpoint.invoke(voice),"second endpoint claim succeeded");
            check((Boolean)allowsUnmanaged.invoke(voice),"unclaimed voice rejected unmanaged session");
            check((Boolean)claimOwner.invoke(voice),"first owner claim failed");
            check(!(Boolean)claimOwner.invoke(voice),"second owner claim succeeded");
            check(!(Boolean)allowsUnmanaged.invoke(voice),"claimed voice still allows unmanaged session");
        } finally { voice.close(); }
        check(!(Boolean)claimOwner.invoke(voice),"closed voice accepted an owner");
        check(!(Boolean)allowsUnmanaged.invoke(voice),"closed voice still allows unmanaged session");
        check(!(Boolean)claimEndpoint.invoke(voice),"closed voice accepted an endpoint");
    }
    private static void activeCallPreventsOwnerTakeover() throws Exception {
        RadioVoiceService voice=new RadioVoiceService("slot1",8,3000);
        Response response=new Response(); Indication indication=new Indication();
        try {
            voice.setResponseFunctions(response.callback,indication.callback);
            check(!voice.injectIncoming("10086",50,1,true),"managed source accepted before claim");
            check(voice.injectIncoming("10086",51,1),"legacy call rejected");
            check(!(Boolean)claimOwner.invoke(voice),"owner claimed an active legacy call");
            check((Boolean)allowsUnmanaged.invoke(voice),"rejected claim disabled existing session");
            voice.rejectCall(904);
            check((Boolean)claimOwner.invoke(voice),"owner claim failed after legacy call ended");
            check(!voice.injectIncoming("10086"),"legacy injection accepted after owner claim");
            check(!voice.injectIncoming("10086",52,1),"legacy associated injection accepted after owner claim");
            check(voice.injectIncoming("10086",52,1,true),"managed incoming rejected after claim");
            check(!voice.injectRemoteHangup(52,1),"legacy hangup cleared managed call");
            count(voice,response,1);
            check(voice.injectRemoteHangup(52,1,true),"managed hangup rejected after claim");
        } finally { voice.close(); }
    }
    private static void queuedCallPreventsOwnerTakeover() throws Exception {
        RadioVoiceService voice=new RadioVoiceService("slot1",8,3000);
        Response response=new Response(); Indication indication=new Indication();
        CountDownLatch entered=new CountDownLatch(1),release=new CountDownLatch(1),claimed=new CountDownLatch(1);
        AtomicReference<Throwable> incomingError=new AtomicReference<>(),claimError=new AtomicReference<>();
        AtomicReference<Boolean> result=new AtomicReference<>();
        Thread incoming=new Thread(() -> {
            try { voice.injectIncoming("10086",53,1); } catch (Throwable error) { incomingError.set(error); }
        });
        Thread claim=new Thread(() -> {
            try { result.set((Boolean)claimOwner.invoke(voice)); }
            catch (Throwable error) { claimError.set(error); }
            finally { claimed.countDown(); }
        });
        try {
            voice.setResponseFunctions(response.callback,indication.callback);
            indication.entered=entered; indication.release=release;
            incoming.start(); check(entered.await(2,TimeUnit.SECONDS),"legacy incoming not entered");
            claim.start(); check(!claimed.await(100,TimeUnit.MILLISECONDS),"owner bypassed queued voice operation");
            release.countDown(); incoming.join(3000); claim.join(3000);
            check(!incoming.isAlive() && !claim.isAlive() && incomingError.get()==null && claimError.get()==null,
                "legacy incoming or owner claim did not finish");
            check(Boolean.FALSE.equals(result.get()),"owner claimed call accepted before takeover");
            count(voice,response,1);
        } finally { release.countDown(); incoming.join(3000); claim.join(3000); voice.close(); }
    }
    private static void queueTimeoutAndRejectionAreObservable() throws Exception {
        RadioVoiceService voice=new RadioVoiceService("slot1",1,400);
        Response response=new Response(); Indication indication=new Indication();
        CountDownLatch entered=new CountDownLatch(1),release=new CountDownLatch(1),removed=new CountDownLatch(1);
        AtomicReference<Throwable> incomingError=new AtomicReference<>();
        Thread incoming=new Thread(() -> {
            try { voice.injectIncoming("10086",41,1); }
            catch (Throwable error) { incomingError.set(error); }
        });
        try {
            voice.setResponseFunctions(response.callback,indication.callback);
            indication.entered=entered; indication.release=release; indication.removed=removed;
            incoming.start(); check(entered.await(2,TimeUnit.SECONDS),"incoming indication not entered");
            incoming.join(3000);
            check(!incoming.isAlive() && incomingError.get() instanceof RemoteException,"blocked incoming did not report timeout");
            try { close(voice,41); throw new AssertionError("blocked cleanup reported success"); }
            catch (RemoteException expected) { }
            try { close(voice,41); throw new AssertionError("full voice queue accepted second cleanup"); }
            catch (RemoteException expected) { }
            release.countDown();
            check(removed.await(2,TimeUnit.SECONDS),"timed-out queued cleanup never finished");
            count(voice,response,0);
        } finally { release.countDown(); incoming.join(3000); voice.close(); }
    }
    private static void uncertainClaimPermanentlyFailsClosed() throws Exception {
        RadioVoiceService voice=new RadioVoiceService("slot1",8,400);
        Response response=new Response(); Indication indication=new Indication();
        CountDownLatch entered=new CountDownLatch(1),release=new CountDownLatch(1);
        AtomicReference<Throwable> incomingError=new AtomicReference<>();
        Thread incoming=new Thread(() -> {
            try { voice.injectIncoming("10086",61,1); }
            catch (Throwable error) { incomingError.set(error); }
        });
        try {
            voice.setResponseFunctions(response.callback,indication.callback);
            indication.entered=entered; indication.release=release;
            incoming.start(); check(entered.await(2,TimeUnit.SECONDS),"incoming indication not entered");
            incoming.join(3000);
            check(!incoming.isAlive() && incomingError.get() instanceof RemoteException,"incoming timeout not observed");
            try { claimOwner.invoke(voice); throw new AssertionError("blocked claim returned success"); }
            catch (InvocationTargetException expected) { check(expected.getCause() instanceof IllegalStateException,"unexpected claim failure"); }
            check(!(Boolean)allowsUnmanaged.invoke(voice),"uncertain claim allowed an unmanaged session");
            release.countDown();
            count(voice,response,1);
            check(!(Boolean)claimOwner.invoke(voice),"uncertain claim allowed another owner");
            voice.rejectCall(905); count(voice,response,0);
            check(!voice.injectIncoming("10086",62,1),"uncertain claim allowed legacy injection");
        } finally { release.countDown(); incoming.join(3000); voice.close(); }
    }
    public static void main(String[] args) throws Exception {
        try {
            closeSession=RadioVoiceService.class.getDeclaredMethod("closeEventSession",long.class);
            claimOwner=RadioVoiceService.class.getDeclaredMethod("claimEventSessionOwner");
            allowsUnmanaged=RadioVoiceService.class.getDeclaredMethod("allowsUnmanagedEventSession");
            claimEndpoint=RadioVoiceService.class.getDeclaredMethod("claimEventEndpoint");
        }
        catch (NoSuchMethodException error) { throw new AssertionError("event session cleanup not implemented",error); }
        closeSession.setAccessible(true); claimOwner.setAccessible(true); allowsUnmanaged.setAccessible(true); claimEndpoint.setAccessible(true);
        identityAndLifecycle(); failedIndicationDoesNotRollback(); rebindDuringNotification(); ownerClaimIsExclusive();
        activeCallPreventsOwnerTakeover(); queuedCallPreventsOwnerTakeover();
        queueTimeoutAndRejectionAreObservable(); uncertainClaimPermanentlyFailsClosed();
        System.out.println("RADIO_VOICE_SESSION_CLOSE_OK");
    }
}
