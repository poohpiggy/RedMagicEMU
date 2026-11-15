package app.aemu.stub;

import android.hardware.radio.RadioError;
import android.hardware.radio.AccessNetwork;
import android.hardware.radio.RadioResponseInfo;
import android.hardware.radio.network.IRadioNetworkIndication;
import android.hardware.radio.network.IRadioNetworkResponse;
import android.hardware.radio.network.IndicationFilter;
import android.hardware.radio.network.SignalStrength;
import android.hardware.radio.network.SignalThresholdInfo;
import android.hardware.radio.modem.IRadioModemResponse;
import android.hardware.radio.modem.IRadioModemIndication;
import android.os.IBinder;
import android.os.RemoteException;
import java.lang.reflect.Proxy;
import java.util.ArrayList;
import java.util.List;
import java.util.Collections;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;

/** Callback lifetime and signal-filter behavior against the shared radio model. */
public final class RadioNetworkSignalIndicationTest {
    private static void check(boolean ok, String message) { if (!ok) throw new AssertionError(message); }
    private static void awaitCount(List<SignalStrength> signals, int oldCount, String message) throws Exception {
        long until = System.nanoTime() + 2_000_000_000L;
        while (signals.size() <= oldCount && System.nanoTime() < until) Thread.sleep(10);
        check(signals.size() > oldCount, message);
    }
    private static final class Peer {
        final List<IBinder.DeathRecipient> deaths = new ArrayList<>();
        final IBinder binder = (IBinder) Proxy.newProxyInstance(IBinder.class.getClassLoader(), new Class<?>[]{IBinder.class}, (p,m,a) -> {
            switch (m.getName()) {
                case "linkToDeath": deaths.add((IBinder.DeathRecipient)a[0]); return null;
                case "unlinkToDeath": return deaths.remove(a[0]);
                case "isBinderAlive": return true;
                case "hashCode": return System.identityHashCode(p);
                case "equals": return p == a[0];
                default: throw new UnsupportedOperationException(m.getName());
            }
        });
        void die() { for (IBinder.DeathRecipient d : new ArrayList<>(deaths)) d.binderDied(); }
    }
    private static final class Callbacks {
        final Peer responsePeer = new Peer(), indicationPeer = new Peer();
        final List<SignalStrength> signals = Collections.synchronizedList(new ArrayList<>());
        RadioResponseInfo result;
        final IRadioNetworkResponse response = (IRadioNetworkResponse) Proxy.newProxyInstance(IRadioNetworkResponse.class.getClassLoader(), new Class<?>[]{IRadioNetworkResponse.class}, (p,m,a) -> {
            if (m.getName().equals("asBinder")) return responsePeer.binder;
            if (a != null && a.length > 0 && a[0] instanceof RadioResponseInfo) { result = (RadioResponseInfo)a[0]; return null; }
            throw new AssertionError("unexpected response " + m.getName());
        });
        final IRadioNetworkIndication indication = (IRadioNetworkIndication) Proxy.newProxyInstance(IRadioNetworkIndication.class.getClassLoader(), new Class<?>[]{IRadioNetworkIndication.class}, (p,m,a) -> {
            if (m.getName().equals("asBinder")) return indicationPeer.binder;
            if (m.getName().equals("currentSignalStrength")) { signals.add((SignalStrength)a[1]); return null; }
            throw new AssertionError("unexpected indication " + m.getName());
        });
    }
    public static void main(String[] args) throws Exception {
        SyntheticSingleSlot model = SyntheticSingleSlot.forTests();
        model.setRadioPower(true);
        RadioNetworkService service = new RadioNetworkService("slot1", model);
        Callbacks first = new Callbacks();
        service.setResponseFunctions(first.response, first.indication);
        awaitCount(first.signals, 0, "initial signal missing");
        int initial = first.signals.size();
        model.setRadioPower(false);
        awaitCount(first.signals, initial, "OFF signal update missing");
        check(first.signals.get(first.signals.size()-1).lte.rsrp == android.hardware.radio.RadioConst.VALUE_UNAVAILABLE, "OFF notification retains LTE signal");
        int off = first.signals.size();
        model.setRadioPower(true);
        awaitCount(first.signals, off, "recovered signal update missing");
        service.setIndicationFilter(10, IndicationFilter.NONE);
        check(first.result.error == RadioError.NONE, "signal filter rejected");
        int suppressed = first.signals.size();
        model.setRadioPower(false);
        model.setRadioPower(true);
        Thread.sleep(100);
        check(first.signals.size() == suppressed, "disabled filter delivered signal");
        service.setIndicationFilter(11, IndicationFilter.SIGNAL_STRENGTH);
        check(first.result.error == RadioError.NONE, "signal filter enable rejected");
        awaitCount(first.signals, suppressed, "enabling filter did not refresh signal");
        SignalThresholdInfo criteria = new SignalThresholdInfo();
        criteria.ran = AccessNetwork.EUTRAN;
        criteria.signalMeasurement = SignalThresholdInfo.SIGNAL_MEASUREMENT_TYPE_RSRP;
        criteria.hysteresisDb = 2;
        criteria.hysteresisMs = 100;
        criteria.thresholds = new int[]{-120, -100, -80};
        criteria.isEnabled = true;
        service.setSignalStrengthReportingCriteria(20, new SignalThresholdInfo[]{criteria});
        check(first.result.error == RadioError.NONE, "valid LTE criteria rejected");
        criteria.isEnabled = false;
        service.setSignalStrengthReportingCriteria(25, new SignalThresholdInfo[]{criteria});
        check(first.result.error == RadioError.NONE, "disabling criteria failed");
        int beforeClear = first.signals.size();
        model.setRadioPower(false);
        awaitCount(first.signals, beforeClear, "disabled criteria did not clear OFF signal");
        int beforeEnable = first.signals.size();
        model.setRadioPower(true);
        Thread.sleep(100);
        check(first.signals.size() == beforeEnable, "disabled criteria emitted available signal");
        criteria.isEnabled = true;
        service.setSignalStrengthReportingCriteria(26, new SignalThresholdInfo[]{criteria});
        check(first.result.error == RadioError.NONE, "enabling criteria failed");
        awaitCount(first.signals, beforeEnable, "enabling criteria did not refresh signal");
        criteria.thresholds = new int[]{-80, -100};
        service.setSignalStrengthReportingCriteria(21, new SignalThresholdInfo[]{criteria});
        check(first.result.error == RadioError.INVALID_ARGUMENTS, "unsorted thresholds accepted");
        criteria.thresholds = new int[]{-120, -100};
        criteria.ran = AccessNetwork.NGRAN;
        service.setSignalStrengthReportingCriteria(22, new SignalThresholdInfo[]{criteria});
        check(first.result.error == RadioError.REQUEST_NOT_SUPPORTED, "unsupported RAT accepted");
        service.setSignalStrengthReportingCriteria(23, null);
        check(first.result.error == RadioError.INVALID_ARGUMENTS, "null criteria accepted");
        service.setSignalStrengthReportingCriteria(24, new SignalThresholdInfo[0]);
        check(first.result.error == RadioError.NONE, "empty criteria rejected");
        Callbacks next = new Callbacks();
        IBinder.DeathRecipient oldDeath = first.responsePeer.deaths.get(0);
        service.setResponseFunctions(next.response, next.indication);
        oldDeath.binderDied();
        int oldCount = first.signals.size(), nextCount = next.signals.size();
        model.setRadioPower(false);
        awaitCount(next.signals, nextCount, "replacement callback missed update");
        check(first.signals.size() == oldCount, "replacement did not isolate old callback");
        next.indicationPeer.die();
        nextCount = next.signals.size();
        model.setRadioPower(true);
        check(next.signals.size() == nextCount && next.responsePeer.deaths.isEmpty(), "dead callback retained or notified");
        try { service.getSignalStrength(12); throw new AssertionError("dead callbacks accepted"); } catch (RemoteException expected) { }
        Callbacks closed = new Callbacks();
        service.setResponseFunctions(closed.response, closed.indication);
        service.close();
        int closedCount = closed.signals.size();
        model.setRadioPower(false);
        check(closed.signals.size() == closedCount && closed.responsePeer.deaths.isEmpty()
                && closed.indicationPeer.deaths.isEmpty(), "closed service retained or notified callbacks");
        try { service.getSignalStrength(13); throw new AssertionError("closed callbacks accepted"); } catch (RemoteException expected) { }
        model.disconnect();
        SyntheticSingleSlot disconnectedModel = SyntheticSingleSlot.forTests();
        disconnectedModel.setRadioPower(true);
        RadioNetworkService disconnectedService = new RadioNetworkService("slot1", disconnectedModel);
        Callbacks disconnected = new Callbacks();
        disconnectedService.setResponseFunctions(disconnected.response, disconnected.indication);
        awaitCount(disconnected.signals, 0, "initial disconnected-model signal missing");
        int beforeDisconnect = disconnected.signals.size();
        disconnectedModel.disconnect();
        awaitCount(disconnected.signals, beforeDisconnect, "disconnect signal update missing");
        check(disconnected.signals.size() > beforeDisconnect
                && disconnected.signals.get(disconnected.signals.size()-1).lte.rsrp == android.hardware.radio.RadioConst.VALUE_UNAVAILABLE,
                "disconnect did not clear the reported signal");
        disconnectedService.close();
        SyntheticSingleSlot failingModel = SyntheticSingleSlot.forTests();
        RadioNetworkService failingNetwork = new RadioNetworkService("slot1", failingModel);
        Callbacks failing = new Callbacks();
        AtomicInteger calls = new AtomicInteger();
        CountDownLatch failed = new CountDownLatch(1);
        IRadioNetworkIndication failingIndication = (IRadioNetworkIndication) Proxy.newProxyInstance(IRadioNetworkIndication.class.getClassLoader(), new Class<?>[]{IRadioNetworkIndication.class}, (p,m,a) -> {
            if (m.getName().equals("asBinder")) return failing.indicationPeer.binder;
            if (m.getName().equals("currentSignalStrength")) {
                if (calls.incrementAndGet() > 1) { failed.countDown(); throw new RemoteException("dead indication"); }
                return null;
            }
            throw new AssertionError("unexpected indication " + m.getName());
        });
        failingNetwork.setResponseFunctions(failing.response, failingIndication);
        long initialDeadline = System.nanoTime() + 2_000_000_000L;
        while (calls.get() == 0 && System.nanoTime() < initialDeadline) Thread.sleep(10);
        check(calls.get() == 1, "initial failure-test signal missing");
        RadioModemService modem = new RadioModemService("slot1", failingModel);
        Peer modemResponsePeer = new Peer(), modemIndicationPeer = new Peer();
        final RadioResponseInfo[] powerResult = {null};
        IRadioModemResponse modemResponse = (IRadioModemResponse) Proxy.newProxyInstance(IRadioModemResponse.class.getClassLoader(), new Class<?>[]{IRadioModemResponse.class}, (p,m,a) -> {
            if (m.getName().equals("asBinder")) return modemResponsePeer.binder;
            if (m.getName().equals("setRadioPowerResponse")) { powerResult[0] = (RadioResponseInfo)a[0]; return null; }
            throw new AssertionError("unexpected modem response " + m.getName());
        });
        IRadioModemIndication modemIndication = (IRadioModemIndication) Proxy.newProxyInstance(IRadioModemIndication.class.getClassLoader(), new Class<?>[]{IRadioModemIndication.class}, (p,m,a) -> {
            if (m.getName().equals("asBinder")) return modemIndicationPeer.binder;
            if (m.getName().equals("rilConnected") || m.getName().equals("radioStateChanged")) return null;
            throw new AssertionError("unexpected modem indication " + m.getName());
        });
        modem.setResponseFunctions(modemResponse, modemIndication);
        modem.setRadioPower(30, true, false, false);
        check(powerResult[0] != null && powerResult[0].error == RadioError.NONE, "signal failure changed power response");
        check(failed.await(2, TimeUnit.SECONDS), "signal callback failure was not exercised");
        failingNetwork.close();
        modem.detachStateListener();
        System.out.println("RADIO_NETWORK_SIGNAL_INDICATION_OK");
    }
}
