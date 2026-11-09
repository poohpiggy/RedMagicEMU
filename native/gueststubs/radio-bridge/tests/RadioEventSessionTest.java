package app.aemu.stub;

import android.hardware.radio.messaging.IRadioMessagingIndication;
import android.hardware.radio.messaging.IRadioMessagingResponse;
import android.hardware.radio.voice.IRadioVoiceIndication;
import android.hardware.radio.voice.IRadioVoiceResponse;
import android.os.IBinder;
import android.os.RemoteException;
import java.lang.reflect.Field;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashSet;
import java.util.Set;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

/** Uses real isolated models; only the framework callbacks are synthetic. */
public final class RadioEventSessionTest {
    private static void check(boolean value, String message) { if (!value) throw new AssertionError(message); }
    private static IBinder peer() {
        return (IBinder) Proxy.newProxyInstance(IBinder.class.getClassLoader(), new Class<?>[]{IBinder.class},
                (p, m, a) -> {
                    switch (m.getName()) {
                        case "linkToDeath": return null;
                        case "unlinkToDeath": return true;
                        case "isBinderAlive": return true;
                        case "hashCode": return System.identityHashCode(p);
                        case "equals": return p == a[0];
                        case "toString": return "synthetic-peer";
                        default: throw new UnsupportedOperationException(m.getName());
                    }
                });
    }
    private static final class Fixture implements AutoCloseable {
        final RadioVoiceService voice = new RadioVoiceService("slot1", 8, 2000);
        final RadioMessagingService sms = RadioMessagingService.withoutStateForTests("slot1");
        final AtomicInteger messages = new AtomicInteger(), states = new AtomicInteger(), rings = new AtomicInteger();
        final AtomicReference<byte[]> pdu = new AtomicReference<>();
        final AtomicReference<CountDownLatch> blockSms = new AtomicReference<>();
        final AtomicReference<Runnable> duringSms = new AtomicReference<>();
        final CountDownLatch smsEntered = new CountDownLatch(1);
        volatile CountDownLatch voiceStateEntered, releaseVoiceState;
        volatile boolean failSms, failVoiceState;
        final RadioEventEndpoint endpoint;

        Fixture() throws Exception {
            IBinder vr = peer(), vi = peer(), sr = peer(), si = peer();
            IRadioVoiceResponse voiceResponse = (IRadioVoiceResponse) Proxy.newProxyInstance(
                    IRadioVoiceResponse.class.getClassLoader(), new Class<?>[]{IRadioVoiceResponse.class},
                    (p, m, a) -> m.getName().equals("asBinder") ? vr : null);
            IRadioVoiceIndication voiceIndication = (IRadioVoiceIndication) Proxy.newProxyInstance(
                    IRadioVoiceIndication.class.getClassLoader(), new Class<?>[]{IRadioVoiceIndication.class},
                    (p, m, a) -> {
                        if (m.getName().equals("asBinder")) return vi;
                        if (m.getName().equals("callStateChanged")) {
                            states.incrementAndGet();
                            CountDownLatch entered=voiceStateEntered, release=releaseVoiceState;
                            if (entered!=null) entered.countDown();
                            if (release!=null && !release.await(8,TimeUnit.SECONDS)) throw new RemoteException("test voice callback timed out");
                            if (failVoiceState) throw new RemoteException("synthetic partial voice indication");
                        }
                        if (m.getName().equals("callRing")) rings.incrementAndGet();
                        return null;
                    });
            IRadioMessagingResponse smsResponse = (IRadioMessagingResponse) Proxy.newProxyInstance(
                    IRadioMessagingResponse.class.getClassLoader(), new Class<?>[]{IRadioMessagingResponse.class},
                    (p, m, a) -> m.getName().equals("asBinder") ? sr : null);
            IRadioMessagingIndication smsIndication = (IRadioMessagingIndication) Proxy.newProxyInstance(
                    IRadioMessagingIndication.class.getClassLoader(), new Class<?>[]{IRadioMessagingIndication.class},
                    (p, m, a) -> {
                        if (m.getName().equals("asBinder")) return si;
                        if (m.getName().equals("newSms")) {
                            messages.incrementAndGet(); smsEntered.countDown();
                            CountDownLatch wait = blockSms.get(); if (wait != null) wait.await();
                            pdu.set(((byte[]) a[1]).clone());
                            Runnable nested = duringSms.get(); if (nested != null) nested.run();
                            if (failSms) throw new RemoteException("synthetic partial indication");
                        }
                        return null;
                    });
            voice.setResponseFunctions(voiceResponse, voiceIndication);
            sms.setResponseFunctions(smsResponse, smsIndication);
            endpoint = RadioEventEndpoint.withoutStateForTests(voice, sms);
        }
        @Override public void close() { voice.close(); }
    }

    private static void put64(byte[] bytes, int offset, long value) {
        for (int i = 7; i >= 0; --i) { bytes[offset + i] = (byte) value; value >>>= 8; }
    }
    private static byte[] frame(long epoch, long sequence, int kind, byte[] payload) {
        byte[] frame = new byte[23 + payload.length];
        frame[0] = 'R'; frame[1] = 'M'; frame[2] = 'E'; frame[3] = '1';
        put64(frame, 4, epoch); put64(frame, 12, sequence); frame[20] = (byte) kind;
        frame[21] = (byte) (payload.length >>> 8); frame[22] = (byte) payload.length;
        System.arraycopy(payload, 0, frame, 23, payload.length); return frame;
    }
    private static void ack(byte[] actual, long epoch, long sequence, int status) {
        check(Arrays.equals(actual, frame(epoch, sequence, 3, new byte[]{(byte) status})), "incorrect ACK frame");
    }
    private static byte[] callId(long epoch, long sequence) {
        byte[] id=new byte[16]; put64(id,0,epoch); put64(id,8,sequence); return id;
    }
    private static void hostHangupUsesIncomingIdentity() throws Exception {
        try (Fixture f = new Fixture()) {
            RadioEventSession session = new RadioEventSession(77, f.endpoint);
            byte[] number = new byte[]{'+','1','0','0','8','6'};
            ack(session.receive(frame(77,1,2,number)),77,1,1);
            ack(session.receive(frame(77,2,4,callId(77,1))),77,2,1);
            check(f.states.get()==2 && f.rings.get()==1,"host hangup did not end first call");
            ack(session.receive(frame(77,3,2,number)),77,3,1);
            ack(session.receive(frame(77,4,4,callId(77,1))),77,4,2);
            check(f.states.get()==3 && f.rings.get()==2,"stale hangup affected new call with same number");
            ack(session.receive(frame(77,5,2,number)),77,5,2);
            f.voice.acceptCall(404);
            ack(session.receive(frame(77,6,4,callId(77,3))),77,6,1);
            check(f.states.get()==5,"matching host hangup did not end answered call");
            check(session.receive(frame(77,6,4,callId(77,3)))==null,"duplicate hangup frame accepted");
            ack(session.receive(frame(77,7,4,callId(77,3))),77,7,2);
        }
    }
    private static void invalidHangupFramesDoNotAdvanceSequence() throws Exception {
        try (Fixture f = new Fixture()) {
            RadioEventSession session = new RadioEventSession(88, f.endpoint);
            check(session.receive(frame(88,1,4,new byte[15]))==null,"short hangup accepted");
            check(session.receive(frame(88,1,4,new byte[17]))==null,"long hangup accepted");
            check(session.receive(frame(88,1,4,callId(0,1)))==null,"zero call epoch accepted");
            check(session.receive(frame(88,1,4,callId(88,0)))==null,"zero call sequence accepted");
            check(session.receive(frame(88,1,4,callId(87,1)))==null,"different call epoch accepted");
            ack(session.receive(frame(88,1,2,new byte[]{'1'})),88,1,1);
            ack(session.receive(frame(88,2,4,callId(88,1))),88,2,1);
        }
    }
    private static void guestRejectAndUncertainHangup() throws Exception {
        try (Fixture f = new Fixture()) {
            RadioEventSession session = new RadioEventSession(91, f.endpoint);
            byte[] number = new byte[]{'1','0','0','8','6'};
            ack(session.receive(frame(91,1,2,number)),91,1,1);
            f.voice.rejectCall(405);
            ack(session.receive(frame(91,2,2,number)),91,2,1);
            ack(session.receive(frame(91,3,4,callId(91,1))),91,3,2);
            f.failVoiceState = true;
            byte[] hangup=frame(91,4,4,callId(91,2));
            ack(session.receive(hangup),91,4,3);
            check(session.receive(hangup)==null,"uncertain hangup replayed");
            f.failVoiceState = false;
            ack(session.receive(frame(91,5,2,number)),91,5,1);
            ack(session.receive(frame(91,6,4,callId(91,2))),91,6,2);
            ack(session.receive(frame(91,7,4,callId(91,5))),91,7,1);
        }
    }
    private static void oldSessionCannotHangupNewSessionCall() throws Exception {
        try (Fixture f = new Fixture()) {
            RadioEventSession oldSession=new RadioEventSession(90,f.endpoint);
            ack(oldSession.receive(frame(90,1,2,new byte[]{'1'})),90,1,1);
            f.voice.rejectCall(406);
            RadioEventSession newSession=new RadioEventSession(91,f.endpoint);
            ack(newSession.receive(frame(91,1,2,new byte[]{'1'})),91,1,1);
            check(newSession.receive(frame(91,2,4,callId(90,1)))==null,"old epoch was accepted by new session");
            ack(newSession.receive(frame(91,2,4,callId(91,1))),91,2,1);
        }
    }
    private static Object invokeLifecycle(Method method, Object target, Object... args) throws Exception {
        try { return method.invoke(target, args); }
        catch (InvocationTargetException wrapped) {
            Throwable cause=wrapped.getCause();
            if (cause instanceof Exception) throw (Exception)cause;
            if (cause instanceof Error) throw (Error)cause;
            throw new AssertionError(cause);
        }
    }
    private static void managedSessionLifecycle() throws Exception {
        final Class<?> managerClass;
        try { managerClass=Class.forName("app.aemu.stub.RadioEventSessionManager"); }
        catch (ClassNotFoundException missing) { throw new AssertionError("managed session lifecycle missing",missing); }
        java.lang.reflect.Constructor<?> constructor=managerClass.getDeclaredConstructor(RadioEventEndpoint.class);
        constructor.setAccessible(true);
        Method open=managerClass.getDeclaredMethod("open"); open.setAccessible(true);
        Method mustExit=managerClass.getDeclaredMethod("mustExitProcess"); mustExit.setAccessible(true);
        Method epoch=RadioEventSession.class.getDeclaredMethod("epoch"); epoch.setAccessible(true);
        Method close=RadioEventSession.class.getDeclaredMethod("closeSession"); close.setAccessible(true);
        Method failure=RadioEventSession.class.getDeclaredMethod("closeFailure"); failure.setAccessible(true);
        Class<?> allocatorClass=Class.forName("app.aemu.stub.RadioEventSessionManager$EpochAllocator");
        java.lang.reflect.Constructor<?> allocatorConstructor=allocatorClass.getDeclaredConstructor(long.class);
        allocatorConstructor.setAccessible(true);
        Method allocate=allocatorClass.getDeclaredMethod("next"); allocate.setAccessible(true);
        Object exhausted=allocatorConstructor.newInstance(-2L);
        check((Long)invokeLifecycle(allocate,exhausted)==-1L,"last unsigned epoch not allocated");
        try { invokeLifecycle(allocate,exhausted); throw new AssertionError("epoch allocator wrapped to zero"); }
        catch (IllegalStateException expected) { }
        Object concurrentAllocator=allocatorConstructor.newInstance(0L);
        Set<Long> allocated=Collections.synchronizedSet(new HashSet<>());
        AtomicReference<Throwable> allocatorFailure=new AtomicReference<>();
        Thread[] allocators=new Thread[4];
        for (int i=0;i<allocators.length;i++) {
            allocators[i]=new Thread(() -> {
                try {
                    for (int j=0;j<100;j++) {
                        long value=(Long)invokeLifecycle(allocate,concurrentAllocator);
                        if (value==0 || !allocated.add(value)) throw new AssertionError("duplicate or zero epoch");
                    }
                } catch(Throwable error) { allocatorFailure.compareAndSet(null,error); }
            });
            allocators[i].start();
        }
        for (Thread allocator:allocators) allocator.join(3000);
        check(allocatorFailure.get()==null && allocated.size()==400,"concurrent epoch allocation failed");
        CountDownLatch legacyRelease=new CountDownLatch(1);
        try (Fixture f=new Fixture()) {
            RadioEventSession legacy=new RadioEventSession(997,f.endpoint);
            f.blockSms.set(legacyRelease);
            AtomicReference<Throwable> legacyError=new AtomicReference<>();
            Thread legacyReceiver=new Thread(() -> { try { legacy.receive(frame(997,1,1,new byte[]{1})); } catch(Throwable e) { legacyError.set(e); } });
            try {
                legacyReceiver.start(); check(f.smsEntered.await(2,TimeUnit.SECONDS),"legacy event not entered");
                try { constructor.newInstance(f.endpoint); throw new AssertionError("owner claimed during legacy event"); }
                catch (InvocationTargetException expected) { check(expected.getCause() instanceof IllegalStateException,"wrong in-flight claim error"); }
            } finally { legacyRelease.countDown(); legacyReceiver.join(3000); }
            check(!legacyReceiver.isAlive() && legacyError.get()==null,"legacy event did not finish");
            Object manager=constructor.newInstance(f.endpoint);
            RadioEventSession managed=(RadioEventSession)invokeLifecycle(open,manager);
            check("CLEANED".equals(invokeLifecycle(close,managed).toString()),"managed session after legacy event did not close");
            check(legacy.receive(frame(997,2,2,new byte[]{'1'}))==null,"preexisting legacy session survived claim");
        } finally { legacyRelease.countDown(); }
        try (Fixture f=new Fixture()) {
            Field globalField=managerClass.getDeclaredField("EPOCHS"); globalField.setAccessible(true);
            Field lastField=allocatorClass.getDeclaredField("last"); lastField.setAccessible(true);
            long upcoming=lastField.getLong(globalField.get(null))+1;
            RadioEventSession unmanaged=new RadioEventSession(upcoming,f.endpoint);
            try { RadioEventEndpoint.withoutStateForTests(f.voice,f.sms); throw new AssertionError("second endpoint claimed one voice service"); }
            catch (IllegalStateException expected) { }
            Object manager=constructor.newInstance(f.endpoint);
            check(unmanaged.receive(frame(upcoming,1,2,new byte[]{'1'}))==null,"old unmanaged session bypassed claimed owner");
            check(f.endpoint.dispatch(2,new byte[]{'1'})==RadioEventEndpoint.Outcome.REJECTED,"direct legacy dispatch bypassed claimed owner");
            try { new RadioEventSession(998,f.endpoint); throw new AssertionError("new unmanaged session bypassed claimed owner"); }
            catch (IllegalStateException expected) { }
            try { constructor.newInstance(f.endpoint); throw new AssertionError("second manager claimed one voice service"); }
            catch (InvocationTargetException expected) { check(expected.getCause() instanceof IllegalStateException,"wrong second manager error"); }
            RadioEventSession a=(RadioEventSession)invokeLifecycle(open,manager);
            long first=(Long)invokeLifecycle(epoch,a);
            check(first==upcoming,"test did not use the managed session's epoch");
            try { invokeLifecycle(open,manager); throw new AssertionError("overlapping managed session accepted"); }
            catch (IllegalStateException expected) { }
            ack(a.receive(frame(first,1,2,new byte[]{'1'})),first,1,1);
            check("UNCERTAIN".equals(invokeLifecycle(close,unmanaged).toString()),"retired unmanaged close was accepted");
            RadioEventSession forged=new RadioEventSession(first,f.endpoint,(RadioEventSessionManager)manager);
            check("UNCERTAIN".equals(invokeLifecycle(close,forged).toString()),"inactive managed close was accepted");
            check(!(Boolean)invokeLifecycle(mustExit,manager),"inactive session poisoned the active manager");
            ack(a.receive(frame(first,2,2,new byte[]{'1'})),first,2,2);
            check("CLEANED".equals(invokeLifecycle(close,a).toString()),"old session cleanup failed");
            check(a.receive(frame(first,3,2,new byte[]{'1'}))==null,"closed session accepted event");
            RadioEventSession b=(RadioEventSession)invokeLifecycle(open,manager);
            long second=(Long)invokeLifecycle(epoch,b);
            check(first!=0 && second!=0 && first!=second,"session epoch reused");
            ack(b.receive(frame(second,1,2,new byte[]{'1'})),second,1,1);
            check("CLEANED".equals(invokeLifecycle(close,a).toString()),"repeat close changed result");
            ack(b.receive(frame(second,2,2,new byte[]{'1'})),second,2,2);
            check("CLEANED".equals(invokeLifecycle(close,b).toString()),"new session cleanup failed");
            check(!(Boolean)invokeLifecycle(mustExit,manager),"successful cleanup poisoned manager");
        }
        try (Fixture f=new Fixture()) {
            Object manager=constructor.newInstance(f.endpoint);
            RadioEventSession session=(RadioEventSession)invokeLifecycle(open,manager);
            long id=(Long)invokeLifecycle(epoch,session);
            ack(session.receive(frame(id,1,2,new byte[]{'1'})),id,1,1);
            f.failVoiceState=true;
            check("UNCERTAIN".equals(invokeLifecycle(close,session).toString()),"failed cleanup reported success");
            check(invokeLifecycle(failure,session)!=null && (Boolean)invokeLifecycle(mustExit,manager),"cleanup failure was hidden");
            try { invokeLifecycle(open,manager); throw new AssertionError("new session after uncertain cleanup accepted"); }
            catch (IllegalStateException expected) { }
        }
        CountDownLatch stateEntered=new CountDownLatch(1),releaseState=new CountDownLatch(1);
        try (Fixture f=new Fixture()) {
            Object manager=constructor.newInstance(f.endpoint);
            RadioEventSession session=(RadioEventSession)invokeLifecycle(open,manager);
            long id=(Long)invokeLifecycle(epoch,session);
            ack(session.receive(frame(id,1,2,new byte[]{'1'})),id,1,1);
            f.voiceStateEntered=stateEntered; f.releaseVoiceState=releaseState;
            AtomicReference<Object> closeResult=new AtomicReference<>();
            AtomicReference<Throwable> closeError=new AtomicReference<>();
            Thread closer=new Thread(() -> { try { closeResult.set(invokeLifecycle(close,session)); } catch(Throwable e) { closeError.set(e); } });
            try {
                closer.start(); check(stateEntered.await(2,TimeUnit.SECONDS),"cleanup callback not entered");
                check("IN_PROGRESS".equals(invokeLifecycle(close,session).toString()),"concurrent close misreported an in-progress cleanup");
                try { invokeLifecycle(open,manager); throw new AssertionError("new session during cleanup accepted"); }
                catch (IllegalStateException expected) { }
            } finally { releaseState.countDown(); closer.join(3000); }
            check(!closer.isAlive() && closeError.get()==null && "CLEANED".equals(closeResult.get().toString()),"cleanup did not finish cleanly");
            RadioEventSession next=(RadioEventSession)invokeLifecycle(open,manager);
            check("CLEANED".equals(invokeLifecycle(close,next).toString()),"new session after completed cleanup did not close");
        } finally { releaseState.countDown(); }
        CountDownLatch pendingEntered=new CountDownLatch(1),releasePending=new CountDownLatch(1);
        try (Fixture f=new Fixture()) {
            Object manager=constructor.newInstance(f.endpoint);
            RadioEventSession session=(RadioEventSession)invokeLifecycle(open,manager);
            long id=(Long)invokeLifecycle(epoch,session);
            f.voiceStateEntered=pendingEntered; f.releaseVoiceState=releasePending;
            AtomicReference<byte[]> reply=new AtomicReference<>();
            AtomicReference<Throwable> pendingError=new AtomicReference<>();
            Thread incoming=new Thread(() -> { try { reply.set(session.receive(frame(id,1,2,new byte[]{'1'}))); } catch(Throwable e) { pendingError.set(e); } });
            try {
                incoming.start(); check(pendingEntered.await(2,TimeUnit.SECONDS),"voice event not entered");
                incoming.join(4000);
                check(!incoming.isAlive() && pendingError.get()==null,"timed-out receive did not finish");
                ack(reply.get(),id,1,3);
                check("UNCERTAIN".equals(invokeLifecycle(close,session).toString()),"queued cleanup timeout was reported as success");
                check((Boolean)invokeLifecycle(mustExit,manager) && invokeLifecycle(failure,session)!=null,"queued cleanup timeout did not require exit");
                try { invokeLifecycle(open,manager); throw new AssertionError("new session accepted after queued cleanup timeout"); }
                catch (IllegalStateException expected) { }
            } finally { releasePending.countDown(); incoming.join(3000); }
        } finally { releasePending.countDown(); }
        CountDownLatch release=new CountDownLatch(1);
        try (Fixture f=new Fixture()) {
            Object manager=constructor.newInstance(f.endpoint);
            RadioEventSession session=(RadioEventSession)invokeLifecycle(open,manager);
            long id=(Long)invokeLifecycle(epoch,session); f.blockSms.set(release);
            AtomicReference<Throwable> receiveError=new AtomicReference<>();
            Thread receiver=new Thread(() -> { try { session.receive(frame(id,1,1,new byte[]{1})); } catch(Throwable e) { receiveError.set(e); } });
            try {
                receiver.start(); check(f.smsEntered.await(2,TimeUnit.SECONDS),"blocked event not entered");
                check("UNCERTAIN".equals(invokeLifecycle(close,session).toString()),"in-flight close falsely claimed cleanup");
                check((Boolean)invokeLifecycle(mustExit,manager) && invokeLifecycle(failure,session)!=null,"in-flight close did not require exit");
                check(session.receive(frame(id,2,2,new byte[]{'1'}))==null,"closing session accepted event");
                try { invokeLifecycle(open,manager); throw new AssertionError("new session after in-flight close accepted"); }
                catch (IllegalStateException expected) { }
            } finally { release.countDown(); receiver.join(3000); }
            check(!receiver.isAlive() && receiveError.get()==null,"blocked event did not finish");
        } finally { release.countDown(); }
    }
    private static void invalidFramesAndOutcomes() throws Exception {
        try (Fixture f = new Fixture()) {
            RadioEventSession session = new RadioEventSession(77, f.endpoint);
            check(session.receive(null) == null, "null frame acknowledged");
            byte[] one = frame(77, 1, 1, new byte[]{0, (byte) 0xff});
            byte[] bad = one.clone(); bad[0] = 'X'; check(session.receive(bad) == null, "bad magic acknowledged");
            check(session.receive(Arrays.copyOf(one, one.length - 1)) == null, "truncated payload acknowledged");
            check(session.receive(frame(76, 1, 1, new byte[]{1})) == null, "wrong epoch acknowledged");
            check(session.receive(frame(77, 2, 1, new byte[]{1})) == null, "sequence gap acknowledged");
            check(session.receive(frame(77, -1L, 1, new byte[]{1})) == null, "unsigned maximum accepted as first sequence");
            check(session.receive(frame(77, 1, 3, new byte[]{1})) == null, "ACK kind accepted as request");
            check(session.receive(frame(77, 1, 1, new byte[0])) == null, "empty payload acknowledged");
            check(session.receive(frame(77, 1, 1, new byte[257])) == null, "oversized payload acknowledged");
            check(f.messages.get() == 0 && f.states.get() == 0, "invalid frame reached a model");
            ack(session.receive(one), 77, 1, 1);
            check(Arrays.equals(f.pdu.get(), new byte[]{0, (byte) 0xff}), "valid binary PDU changed");
            check(session.receive(one) == null && f.messages.get() == 1, "duplicate caused another SMS");
            ack(session.receive(frame(77, 2, 2, new byte[]{'1', 'x'})), 77, 2, 2);
            check(f.states.get() == 0, "invalid number reached call model");
            ack(session.receive(frame(77, 3, 2, new byte[]{'+', '1', '0'})), 77, 3, 1);
            check(f.states.get() == 1 && f.rings.get() == 1, "valid call was not submitted once");
            ack(session.receive(frame(77, 4, 2, new byte[]{'1'})), 77, 4, 2);
            check(f.states.get() == 1 && f.rings.get() == 1, "busy call emitted another indication");
        }
    }
    private static void uncertainAndReplay() throws Exception {
        try (Fixture f = new Fixture()) {
            RadioEventSession session = new RadioEventSession(42, f.endpoint); f.failSms = true;
            byte[] request = frame(42, 1, 1, new byte[]{3, 4});
            ack(session.receive(request), 42, 1, 3);
            check(f.messages.get() == 1, "uncertain callback was repeated");
            check(session.receive(request) == null && f.messages.get() == 1, "uncertain frame replayed");
            // The first callback is uncertain, but the pending guard rejects this
            // second request before invoking any callback.
            ack(session.receive(frame(42, 2, 1, new byte[]{5})), 42, 2, 2);
            check(f.messages.get() == 1, "pending SMS invoked callback again");
        }
    }
    private static void concurrentDuplicateAndCopy() throws Exception {
        CountDownLatch release = new CountDownLatch(1);
        try (Fixture f = new Fixture()) {
            RadioEventSession session = new RadioEventSession(13, f.endpoint); f.blockSms.set(release);
            byte[] request = frame(13, 1, 1, new byte[]{7, 8});
            AtomicReference<byte[]> first = new AtomicReference<>(), second = new AtomicReference<>();
            AtomicReference<Throwable> error = new AtomicReference<>();
            Thread one = new Thread(() -> { try { first.set(session.receive(request)); } catch (Throwable e) { error.set(e); } });
            Thread two = new Thread(() -> { try { second.set(session.receive(frame(13, 1, 1, new byte[]{7, 8}))); }
                                             catch (Throwable e) { error.set(e); } });
            try {
                one.start(); check(f.smsEntered.await(2, TimeUnit.SECONDS), "callback not entered");
                request[23] = 99; two.start(); release.countDown(); one.join(3000); two.join(3000);
                check(!one.isAlive() && !two.isAlive() && error.get() == null, "concurrent receive did not finish");
                ack(first.get(), 13, 1, 1);
                check(second.get() == null && f.messages.get() == 1, "concurrent duplicate caused second submission");
                check(Arrays.equals(f.pdu.get(), new byte[]{7, 8}), "caller mutated payload during dispatch");
            } finally { release.countDown(); one.join(3000); two.join(3000); }
        } finally { release.countDown(); }
    }
    private static void sequenceExhaustion() throws Exception {
        try (Fixture f = new Fixture()) {
            RadioEventSession session = new RadioEventSession(99, f.endpoint);
            Field last = RadioEventSession.class.getDeclaredField("lastSequence"); last.setAccessible(true); last.setLong(session, -1L);
            check(session.receive(frame(99, 0, 1, new byte[]{1})) == null, "sequence wrapped to zero");
            check(session.receive(frame(99, -1L, 1, new byte[]{1})) == null, "exhausted sequence replayed");
            check(f.messages.get() == 0, "exhausted sequence reached the model");
        }
    }
    private static void rejectReentrantDispatch() throws Exception {
        try (Fixture f = new Fixture()) {
            RadioEventSession session = new RadioEventSession(11, f.endpoint);
            AtomicReference<byte[]> nestedAck = new AtomicReference<>();
            f.duringSms.set(() -> nestedAck.set(session.receive(frame(11, 2, 2, new byte[]{'1'}))));
            ack(session.receive(frame(11, 1, 1, new byte[]{3})), 11, 1, 1);
            check(nestedAck.get() == null, "nested receive bypassed single-flight ordering");
            check(f.states.get() == 0 && f.rings.get() == 0, "nested receive submitted another event");
            ack(session.receive(frame(11, 2, 2, new byte[]{'1'})), 11, 2, 1);
            check(f.states.get() == 1 && f.rings.get() == 1, "next frame could not be submitted normally");
        }
    }
    private static void epochBoundaries() throws Exception {
        try (Fixture f = new Fixture()) {
            try { new RadioEventSession(0, f.endpoint); throw new AssertionError("zero epoch accepted"); }
            catch (IllegalArgumentException expected) { /* no valid zero epoch */ }
            try { new RadioEventSession(1, null); throw new AssertionError("null endpoint accepted"); }
            catch (IllegalArgumentException expected) { /* no model to dispatch to */ }
            RadioEventSession session = new RadioEventSession(-1L, f.endpoint);
            ack(session.receive(frame(-1L, 1, 1, new byte[]{9})), -1L, 1, 1);
            check(f.messages.get() == 1, "unsigned maximum epoch did not round-trip");
        }
    }
    public static void main(String[] args) throws Exception {
        invalidFramesAndOutcomes(); uncertainAndReplay(); concurrentDuplicateAndCopy(); sequenceExhaustion(); epochBoundaries();
        rejectReentrantDispatch(); hostHangupUsesIncomingIdentity(); invalidHangupFramesDoNotAdvanceSequence();
        guestRejectAndUncertainHangup(); oldSessionCannotHangupNewSessionCall(); managedSessionLifecycle();
        System.out.println("RADIO_EVENT_SESSION_OK");
    }
}
