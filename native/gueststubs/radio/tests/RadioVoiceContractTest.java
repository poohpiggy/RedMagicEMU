package app.aemu.stub;

import android.hardware.radio.RadioError;
import android.hardware.radio.RadioResponseInfo;
import android.hardware.radio.voice.Call;
import android.hardware.radio.voice.IRadioVoice;
import android.hardware.radio.voice.IRadioVoiceIndication;
import android.hardware.radio.voice.IRadioVoiceResponse;
import android.os.IBinder;
import android.os.RemoteException;

import java.lang.reflect.Array;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.ArrayList;
import java.util.List;

/** Runs in an isolated test process against the real Android 16 radio interfaces. */
public final class RadioVoiceContractTest {
    private static void check(boolean value, String reason) {
        if (!value) throw new AssertionError(reason);
    }

    private static final class Peer {
        final List<IBinder.DeathRecipient> deaths = new ArrayList<>();
        boolean failLink;
        final IBinder binder = (IBinder) Proxy.newProxyInstance(IBinder.class.getClassLoader(),
                new Class<?>[]{IBinder.class}, (proxy, method, args) -> {
                    switch (method.getName()) {
                        case "linkToDeath":
                            if (failLink) throw new RemoteException("synthetic death-link failure");
                            deaths.add((IBinder.DeathRecipient) args[0]); return null;
                        case "unlinkToDeath": return deaths.remove(args[0]);
                        case "isBinderAlive": return true;
                        case "hashCode": return System.identityHashCode(proxy);
                        case "equals": return proxy == args[0];
                        case "toString": return "synthetic-peer";
                        default: throw new UnsupportedOperationException(method.getName());
                    }
                });
        void die() { for (IBinder.DeathRecipient d : new ArrayList<>(deaths)) d.binderDied(); }
    }

    private static final class Response {
        final Peer peer = new Peer();
        String name;
        RadioResponseInfo info;
        Object[] arguments;
        final IRadioVoiceResponse proxy = (IRadioVoiceResponse) Proxy.newProxyInstance(
                IRadioVoiceResponse.class.getClassLoader(), new Class<?>[]{IRadioVoiceResponse.class},
                (p, m, a) -> {
                    if (m.getName().equals("asBinder")) return peer.binder;
                    if (m.getName().equals("toString")) return "synthetic-response";
                    name = m.getName();
                    arguments = a;
                    info = a != null && a.length > 0 && a[0] instanceof RadioResponseInfo
                            ? (RadioResponseInfo) a[0] : null;
                    return null;
                });
    }

    private static final class Indication {
        final Peer peer = new Peer();
        final List<String> events = new ArrayList<>();
        boolean failRing;
        final IRadioVoiceIndication proxy = (IRadioVoiceIndication) Proxy.newProxyInstance(IRadioVoiceIndication.class.getClassLoader(),
                new Class<?>[]{IRadioVoiceIndication.class}, (p, m, a) -> {
                    if (m.getName().equals("asBinder")) return peer.binder;
                    if (m.getName().equals("toString")) return "synthetic-indication";
                    if (m.getName().equals("callStateChanged")) { events.add("state"); return null; }
                    if (m.getName().equals("callRing")) {
                        if (failRing) throw new RemoteException("synthetic ring failure");
                        events.add("ring"); return null;
                    }
                    throw new UnsupportedOperationException(m.getName());
                });
    }

    private static void result(Response r, String expected, int serial, int error) {
        check(expected.equals(r.name), "wrong callback " + r.name);
        check(r.info != null && r.info.serial == serial && r.info.error == error, "wrong response status");
    }

    private static Object argument(Class<?> type, int serial) {
        if (type == int.class) return serial;
        if (type == boolean.class) return false;
        if (type == String.class) return "";
        if (type.isArray()) return Array.newInstance(type.getComponentType(), 0);
        return null;
    }

    public static void main(String[] args) throws Exception {
        try { new RadioVoiceService("slot2"); throw new AssertionError("slot2 accepted"); }
        catch (IllegalArgumentException expected) { /* one simulated card only */ }
        RadioVoiceService service = new RadioVoiceService("slot1");
        try { service.getCurrentCalls(1); throw new AssertionError("missing callback succeeded"); }
        catch (RemoteException expected) { /* explicit failure, not an empty success */ }
        Response first = new Response(); Indication firstIndication = new Indication();
        service.setResponseFunctions(first.proxy, firstIndication.proxy);
        check(first.peer.deaths.size() == 1 && firstIndication.peer.deaths.size() == 1, "death not tracked");
        Response bad = new Response(); Indication badIndication = new Indication();
        badIndication.peer.failLink = true;
        try { service.setResponseFunctions(bad.proxy, badIndication.proxy); throw new AssertionError("failed bind accepted"); }
        catch (RemoteException expected) { /* old registration must remain active */ }
        check(bad.peer.deaths.isEmpty(), "failed registration leaked death recipient");
        service.getCurrentCalls(9); result(first, "getCurrentCallsResponse", 9, RadioError.NONE);
        IBinder.DeathRecipient oldDeath = first.peer.deaths.get(0);
        int serial = 10;
        for (Method m : IRadioVoice.class.getDeclaredMethods()) {
            String name = m.getName();
            if (name.equals("setResponseFunctions") || name.equals("responseAcknowledgement") ||
                    name.equals("getInterfaceVersion") || name.equals("getInterfaceHash") ||
                    name.equals("getCurrentCalls") || name.equals("acceptCall") ||
                    name.equals("hangup") || name.equals("rejectCall") ||
                    name.equals("hangupForegroundResumeBackground") ||
                    name.equals("hangupWaitingOrBackground")) continue;
            Class<?>[] types = m.getParameterTypes();
            check(types.length > 0 && types[0] == int.class, "unexpected request signature: " + name);
            Object[] values = new Object[types.length];
            for (int i = 0; i < types.length; i++) values[i] = argument(types[i], i == 0 ? serial : 0);
            first.name = null; first.info = null;
            try { m.invoke(service, values); }
            catch (InvocationTargetException e) { throw new AssertionError("request threw: " + name, e.getCause()); }
            String expected = (name.equals("hangup") ? "hangupConnection" : name) + "Response";
            check(expected.equals(first.name), "wrong callback: " + name + " -> " + first.name);
            check(first.info != null && first.info.serial == serial && first.info.error == RadioError.REQUEST_NOT_SUPPORTED,
                    "wrong serial/error: " + name);
            serial++;
        }
        service.getCurrentCalls(201);
        result(first, "getCurrentCallsResponse", 201, RadioError.NONE);
        check(((Call[]) first.arguments[1]).length == 0, "initial call list not empty");
        check(!service.injectIncoming("not-a-number"), "invalid number accepted");
        check(service.injectIncoming("+10000000000"), "incoming call rejected");
        check(!service.injectIncoming("+10000000001"), "second call accepted");
        check(firstIndication.events.size() == 2 && firstIndication.events.get(0).equals("state") &&
                firstIndication.events.get(1).equals("ring"), "incoming indications incorrect");
        service.getCurrentCalls(202);
        result(first, "getCurrentCallsResponse", 202, RadioError.NONE);
        Call[] incoming = (Call[]) first.arguments[1];
        check(incoming.length == 1 && incoming[0].index == 1 && incoming[0].state == Call.STATE_INCOMING &&
                incoming[0].isMT && incoming[0].isVoice && "+10000000000".equals(incoming[0].number), "incoming call incorrect");
        service.acceptCall(203); result(first, "acceptCallResponse", 203, RadioError.NONE);
        service.getCurrentCalls(204); result(first, "getCurrentCallsResponse", 204, RadioError.NONE);
        check(((Call[]) first.arguments[1])[0].state == Call.STATE_ACTIVE, "call not active");
        service.hangup(205, 2); result(first, "hangupConnectionResponse", 205, RadioError.INVALID_CALL_ID);
        service.hangup(206, 1); result(first, "hangupConnectionResponse", 206, RadioError.NONE);
        service.getCurrentCalls(207); result(first, "getCurrentCallsResponse", 207, RadioError.NONE);
        check(((Call[]) first.arguments[1]).length == 0, "hangup did not clear call");
        service.acceptCall(208); result(first, "acceptCallResponse", 208, RadioError.INVALID_STATE);
        check(service.injectIncoming("10000000000"), "second incoming call rejected");
        service.rejectCall(209); result(first, "rejectCallResponse", 209, RadioError.NONE);
        service.getCurrentCalls(210); check(((Call[]) first.arguments[1]).length == 0, "reject did not clear call");

        // A ringing call rejected via hangupWaitingOrBackground must release the slot.
        service.hangupWaitingOrBackground(400);
        result(first, "hangupWaitingOrBackgroundResponse", 400, RadioError.INVALID_STATE);
        check(service.injectIncoming("10000000000"), "background hangup incoming rejected");
        int notificationsBeforeBackgroundHangup = firstIndication.events.size();
        service.hangupWaitingOrBackground(401);
        result(first, "hangupWaitingOrBackgroundResponse", 401, RadioError.NONE);
        check(firstIndication.events.size() == notificationsBeforeBackgroundHangup + 1 &&
                firstIndication.events.get(notificationsBeforeBackgroundHangup).equals("state"),
                "background hangup did not signal state change");
        service.getCurrentCalls(402); result(first, "getCurrentCallsResponse", 402, RadioError.NONE);
        check(((Call[]) first.arguments[1]).length == 0, "background hangup did not clear ringing call");
        check(service.injectIncoming("10000000000"), "incoming after background hangup rejected");
        service.acceptCall(403); result(first, "acceptCallResponse", 403, RadioError.NONE);
        int notificationsBeforeActiveHangup = firstIndication.events.size();
        service.hangupWaitingOrBackground(404);
        result(first, "hangupWaitingOrBackgroundResponse", 404, RadioError.INVALID_STATE);
        check(firstIndication.events.size() == notificationsBeforeActiveHangup,
                "invalid background hangup signalled a state change");
        service.getCurrentCalls(405); result(first, "getCurrentCallsResponse", 405, RadioError.NONE);
        Call[] stillActive = (Call[]) first.arguments[1];
        check(stillActive.length == 1 && stillActive[0].state == Call.STATE_ACTIVE,
                "background hangup cleared active call");
        service.hangup(406, 1); result(first, "hangupConnectionResponse", 406, RadioError.NONE);

        service.hangupForegroundResumeBackground(219);
        result(first, "hangupForegroundResumeBackgroundResponse", 219, RadioError.INVALID_STATE);
        check(service.injectIncoming("10000000000"), "foreground test incoming rejected");
        service.acceptCall(220); result(first, "acceptCallResponse", 220, RadioError.NONE);
        service.hangupForegroundResumeBackground(221);
        result(first, "hangupForegroundResumeBackgroundResponse", 221, RadioError.NONE);
        service.getCurrentCalls(222); result(first, "getCurrentCallsResponse", 222, RadioError.NONE);
        check(((Call[]) first.arguments[1]).length == 0, "foreground hangup did not clear call");
        check(service.injectIncoming("10000000000"), "incoming after foreground hangup rejected");
        service.rejectCall(223); result(first, "rejectCallResponse", 223, RadioError.NONE);

        firstIndication.failRing = true;
        try { service.injectIncoming("+10000000000"); throw new AssertionError("ring failure hidden"); }
        catch (RemoteException expected) { /* prior state indication may already have been seen */ }
        service.getCurrentCalls(211);
        check(((Call[]) first.arguments[1]).length == 1, "partially notified call was falsely rolled back");

        Response next = new Response(); Indication nextIndication = new Indication();
        service.setResponseFunctions(next.proxy, nextIndication.proxy);
        oldDeath.binderDied();
        service.getCurrentCalls(300);
        check(next.info != null && next.info.serial == 300, "old death cleared new callbacks");
        next.peer.die();
        try { service.getCurrentCalls(301); throw new AssertionError("dead callback succeeded"); }
        catch (RemoteException expected) { /* death invalidates the pair */ }
        check(nextIndication.peer.deaths.isEmpty(), "remaining death recipient not unlinked");
        Response afterDeath = new Response(); Indication afterDeathIndication = new Indication();
        service.setResponseFunctions(afterDeath.proxy, afterDeathIndication.proxy);
        service.getCurrentCalls(302); result(afterDeath, "getCurrentCallsResponse", 302, RadioError.NONE);
        check(((Call[]) afterDeath.arguments[1]).length == 0, "call survived callback death");
        System.out.println("RADIO_VOICE_CONTRACT_OK unsupported=" + (serial - 10) + " state=single-call");
    }
}
