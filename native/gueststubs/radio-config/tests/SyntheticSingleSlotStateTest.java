package app.aemu.stub;

import android.hardware.radio.modem.RadioState;
import android.os.RemoteException;

/** One virtual modem; no TelephonyManager, host identity or service registration. */
public final class SyntheticSingleSlotStateTest {
    private static void check(boolean ok, String message) { if (!ok) throw new AssertionError(message); }

    public static void main(String[] args) throws Exception {
        SyntheticSingleSlot state = SyntheticSingleSlot.forTests();
        check(state.isConnected(), "new test session should be connected");
        check(state.radioState() == RadioState.OFF, "new radio must be off");
        check(!state.isNetworkRegistered() && state.isSimReady(), "connected synthetic USIM application not ready");
        check(!state.canDeliverVoiceOrSms(), "events allowed without a ready virtual network");
        check(state.liveModems() == 1 && state.slotStatus().length == 2, "incorrect modem or physical slot count");
        check(state.setRadioPower(true), "power on was not a transition");
        check(state.radioState() == RadioState.ON, "power on not reflected");
        check(state.isNetworkRegistered() && state.isSimReady(), "powered synthetic network or USIM unavailable");
        check(state.canDeliverVoiceOrSms(), "powered registered synthetic SIM rejected events");
        long beforePowerOff = state.deliveryToken();
        check(beforePowerOff != 0, "ready model has no delivery token");
        check(!state.setRadioPower(true), "duplicate power on should not be a transition");
        check(state.setRadioPower(false) && state.radioState() == RadioState.OFF, "power off not reflected");
        check(!state.isNetworkRegistered(), "powered off network remained registered");
        check(state.isSimReady() && !state.canDeliverVoiceOrSms(), "power off changed application state or allowed events");
        check(!state.isCurrentDelivery(beforePowerOff), "power off did not invalidate event token");
        state.setRadioPower(true);
        state.disconnect();
        SyntheticSingleSlot failureState = SyntheticSingleSlot.forTests();
        final int[] notifications = {0};
        failureState.addListener(() -> { notifications[0]++; throw new RemoteException("test failure"); });
        failureState.addListener(() -> notifications[0]++);
        try { failureState.disconnect(); throw new AssertionError("delivery failure was swallowed"); }
        catch (RemoteException expected) { /* state is already unavailable; delivery may be partial */ }
        check(notifications[0] == 2, "one failed listener prevented later notifications");
        check(failureState.radioState() == RadioState.UNAVAILABLE, "failure rolled back disconnect");
        SyntheticSingleSlot powerFailure = SyntheticSingleSlot.forTests();
        powerFailure.setRadioPower(true);
        final int[] powerNotifications = {0};
        powerFailure.addDeliveryListener(() -> { powerNotifications[0]++; throw new IllegalStateException("listener failed"); });
        powerFailure.addDeliveryListener(() -> powerNotifications[0]++);
        try { powerFailure.setRadioPower(false); throw new AssertionError("power listener failure was swallowed"); }
        catch (IllegalStateException mislabeled) { throw new AssertionError("listener failure mistaken for disconnection", mislabeled); }
        catch (RuntimeException expected) { /* transition happened; notification failed */ }
        check(powerNotifications[0] == 2, "failed power listener prevented later notifications");
        check(powerFailure.radioState() == RadioState.OFF, "listener failure rolled back power off");
        check(!state.isConnected() && state.radioState() == RadioState.UNAVAILABLE, "disconnect must make radio unavailable");
        check(!state.isSimReady(), "disconnected synthetic SIM remained available");
        check(!state.isNetworkRegistered(), "disconnected network remained registered");
        check(!state.canDeliverVoiceOrSms(), "events allowed after disconnect");
        try { state.setRadioPower(true); throw new AssertionError("disconnected session restarted"); }
        catch (IllegalStateException expected) { /* construct a fresh session instead */ }
        check(state.radioState() == RadioState.UNAVAILABLE, "failed restart changed state");
        state.disconnect();
        System.out.println("SYNTHETIC_SINGLE_SLOT_STATE_OK");
    }
}
