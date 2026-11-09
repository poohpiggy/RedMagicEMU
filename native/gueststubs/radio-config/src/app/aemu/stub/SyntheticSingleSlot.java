package app.aemu.stub;

import android.hardware.radio.config.MultipleEnabledProfilesMode;
import android.hardware.radio.config.PhoneCapability;
import android.hardware.radio.config.SimPortInfo;
import android.hardware.radio.config.SimSlotStatus;
import android.hardware.radio.modem.RadioState;
import android.os.RemoteException;
import java.util.ArrayList;
import java.util.List;

/** Explicit test-only identity. Never reads or derives values from a real SIM or host device. */
class SyntheticSingleSlot {
    static final String TEST_ICCID = SyntheticSimProfile.TEST_ICCID;
    static final String TEST_ATR = SyntheticSimProfile.TEST_ATR;
    private static final int CARD_PRESENT = 1; // android.hardware.radio.sim.CardStatus.STATE_PRESENT
    private boolean connected = true;
    private boolean radioOn;
    private long deliveryGeneration = 1;
    private final SyntheticSimProfile simProfile = new SyntheticSimProfile();
    interface Listener { void onRadioUnavailable() throws RemoteException; }
    static final class PowerNotificationException extends RuntimeException {
        PowerNotificationException(RuntimeException cause) { super("radio power changed but notification failed", cause); }
    }
    private final List<Listener> listeners = new ArrayList<>();
    private final List<Runnable> deliveryListeners = new ArrayList<>();
    SyntheticSingleSlot() {}

    static SyntheticSingleSlot forTests() { return new SyntheticSingleSlot(); }
    SyntheticSimProfile simProfile() { return simProfile; }

    synchronized boolean isConnected() { return connected; }

    synchronized int radioState() {
        return !connected ? RadioState.UNAVAILABLE : (radioOn ? RadioState.ON : RadioState.OFF);
    }

    /** Model state only; it does not imply framework records are loaded or callbacks are bound. */
    synchronized boolean isNetworkRegistered() { return connected && radioOn; }
    synchronized boolean isSimReady() { return connected; }
    synchronized boolean canDeliverVoiceOrSms() {
        return connected && radioOn && isNetworkRegistered() && isSimReady();
    }

    /** A readiness transition invalidates every event already in flight. */
    synchronized long deliveryToken() { return canDeliverVoiceOrSms() ? deliveryGeneration : 0; }
    synchronized boolean isCurrentDelivery(long token) {
        return token != 0 && deliveryGeneration == token && canDeliverVoiceOrSms();
    }
    private void invalidateDelivery() {
        if (++deliveryGeneration == 0) ++deliveryGeneration;
    }

    boolean setRadioPower(boolean on) {
        boolean changed;
        List<Runnable> notify;
        synchronized (this) {
            if (!connected) throw new IllegalStateException("virtual radio session is disconnected");
            changed = radioOn != on;
            radioOn = on;
            if (changed) invalidateDelivery();
            notify = changed && !on ? new ArrayList<>(deliveryListeners) : new ArrayList<>();
        }
        // Listeners must never run under the shared state lock.
        RuntimeException failure = null;
        for (Runnable listener : notify) {
            try { listener.run(); }
            catch (RuntimeException error) {
                if (failure == null) failure = new PowerNotificationException(error);
                else if (failure.getCause() != error) failure.addSuppressed(error);
            }
        }
        if (failure != null) throw failure;
        return changed;
    }

    synchronized void addDeliveryListener(Runnable listener) {
        if (listener == null) throw new IllegalArgumentException("listener is required");
        deliveryListeners.add(listener);
    }

    synchronized void removeDeliveryListener(Runnable listener) { deliveryListeners.remove(listener); }

    synchronized void addListener(Listener listener) {
        if (listener == null) throw new IllegalArgumentException("listener is required");
        listeners.add(listener);
    }

    synchronized void removeListener(Listener listener) { listeners.remove(listener); }

    void disconnect() throws RemoteException {
        List<Listener> pending;
        List<Runnable> deliveryPending;
        synchronized (this) {
            if (!connected) return;
            connected = false;
            radioOn = false;
            invalidateDelivery();
            pending = new ArrayList<>(listeners);
            deliveryPending = new ArrayList<>(deliveryListeners);
        }
        Throwable failure = null;
        for (Runnable listener : deliveryPending) {
            try { listener.run(); }
            catch (RuntimeException error) {
                if (failure == null) failure = error;
                else if (failure != error) failure.addSuppressed(error);
            }
        }
        for (Listener listener : pending) {
            try { listener.onRadioUnavailable(); }
            catch (RemoteException | RuntimeException error) {
                if (failure == null) failure = error;
                else if (failure != error) failure.addSuppressed(error);
            }
        }
        if (failure instanceof RemoteException) throw (RemoteException) failure;
        if (failure instanceof RuntimeException) throw (RuntimeException) failure;
    }

    byte liveModems() { return 1; }

    SimSlotStatus[] slotStatus() {
        SimPortInfo port = new SimPortInfo();
        port.iccId = simProfile.iccid();
        port.logicalSlotId = 0; // Radio service instance slot1 maps to logical modem 0.
        port.portActive = true;
        SimSlotStatus slot = new SimSlotStatus();
        slot.cardState = CARD_PRESENT;
        slot.atr = simProfile.atr();
        slot.eid = ""; // physical-style simulated card, not an eUICC
        slot.portInfo = new SimPortInfo[]{port};
        slot.supportedMepMode = MultipleEnabledProfilesMode.NONE;
        SimPortInfo emptyPort = new SimPortInfo();
        emptyPort.iccId = "";
        emptyPort.logicalSlotId = -1;
        emptyPort.portActive = false;
        SimSlotStatus emptySlot = new SimSlotStatus();
        emptySlot.cardState = android.hardware.radio.sim.CardStatus.STATE_ABSENT;
        emptySlot.atr = "";
        emptySlot.eid = "";
        emptySlot.portInfo = new SimPortInfo[]{emptyPort};
        emptySlot.supportedMepMode = MultipleEnabledProfilesMode.NONE;
        return new SimSlotStatus[]{slot, emptySlot};
    }

    PhoneCapability phoneCapability() {
        PhoneCapability capability = new PhoneCapability();
        capability.logicalModemIds = new byte[]{0};
        capability.maxActiveVoice = 1;
        // The isolated voice/SMS test model has no data or Internet bearer.
        capability.maxActiveData = 0;
        capability.maxActiveInternetData = 0;
        capability.isInternetLingeringSupported = false;
        return capability;
    }
}
