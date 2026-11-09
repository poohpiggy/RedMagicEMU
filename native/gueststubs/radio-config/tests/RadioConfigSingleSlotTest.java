package app.aemu.stub;

import android.hardware.radio.RadioError;
import android.hardware.radio.RadioResponseInfo;
import android.hardware.radio.config.IRadioConfigIndication;
import android.hardware.radio.config.IRadioConfigResponse;
import android.hardware.radio.config.MultipleEnabledProfilesMode;
import android.hardware.radio.config.PhoneCapability;
import android.hardware.radio.config.SimSlotStatus;
import android.os.IBinder;
import java.lang.reflect.Proxy;
import java.util.Arrays;

/** One synthetic modem, with a present card and an empty physical slot. */
public final class RadioConfigSingleSlotTest {
    private static void check(boolean ok, String reason) { if (!ok) throw new AssertionError(reason); }
    private static IBinder binder() {
        return (IBinder)Proxy.newProxyInstance(IBinder.class.getClassLoader(), new Class<?>[]{IBinder.class},
                (p,m,a) -> {
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
    private static final class Peer {
        final IBinder responseBinder = binder(), indicationBinder = binder();
        String method;
        RadioResponseInfo info;
        Object value;
        final IRadioConfigResponse response = (IRadioConfigResponse)Proxy.newProxyInstance(
                IRadioConfigResponse.class.getClassLoader(), new Class<?>[]{IRadioConfigResponse.class},
                (p,m,a) -> {
                    if (m.getName().equals("asBinder")) return responseBinder;
                    if (m.getName().equals("toString")) return "synthetic-response";
                    method=m.getName(); info=(RadioResponseInfo)a[0]; value=a.length>1?a[1]:null;
                    return null;
                });
        final IRadioConfigIndication indication = (IRadioConfigIndication)Proxy.newProxyInstance(
                IRadioConfigIndication.class.getClassLoader(), new Class<?>[]{IRadioConfigIndication.class},
                (p,m,a) -> {
                    if (m.getName().equals("asBinder")) return indicationBinder;
                    if (m.getName().equals("toString")) return "synthetic-indication";
                    throw new UnsupportedOperationException(m.getName());
                });
    }
    private static void response(Peer p, String method, int serial, int error) {
        check(method.equals(p.method), "wrong callback: " + method);
        check(p.info != null && p.info.serial == serial && p.info.error == error, "wrong serial/error: " + method);
    }
    public static void main(String[] args) throws Exception {
        Peer p = new Peer();
        RadioConfigService unconfigured = new RadioConfigService("slot1");
        unconfigured.setResponseFunctions(p.response,p.indication);
        unconfigured.getNumOfLiveModems(1);
        response(p,"getNumOfLiveModemsResponse",1,RadioError.REQUEST_NOT_SUPPORTED);
        SyntheticSingleSlot model = SyntheticSingleSlot.forTests();
        RadioConfigService s = new RadioConfigService("slot1", model);
        s.setResponseFunctions(p.response,p.indication);
        s.getNumOfLiveModems(10);
        response(p,"getNumOfLiveModemsResponse",10,RadioError.NONE);
        check(((Byte)p.value).byteValue()==1, "not exactly one simulated modem");
        s.getSimSlotsStatus(11);
        response(p,"getSimSlotsStatusResponse",11,RadioError.NONE);
        SimSlotStatus[] slots=(SimSlotStatus[])p.value;
        check(slots.length==2, "expected two physical slots");
        check(slots[0].cardState==1 && slots[0].portInfo.length==1, "first slot is not a present virtual card");
        check(slots[0].portInfo[0].portActive && slots[0].portInfo[0].logicalSlotId==0, "incorrect port mapping");
        check(SyntheticSingleSlot.TEST_ICCID.equals(slots[0].portInfo[0].iccId), "not the synthetic identifier");
        check(SyntheticSingleSlot.TEST_ATR.equals(slots[0].atr) && "".equals(slots[0].eid), "unexpected identifiers");
        check(slots[0].supportedMepMode==MultipleEnabledProfilesMode.NONE, "multiple enabled profiles");
        check(slots[1].cardState==android.hardware.radio.sim.CardStatus.STATE_ABSENT, "second slot is not empty");
        check(slots[1].portInfo.length==1 && !slots[1].portInfo[0].portActive
                && slots[1].portInfo[0].logicalSlotId==-1, "second slot has an active or mapped modem");
        check("".equals(slots[1].portInfo[0].iccId) && "".equals(slots[1].atr)
                && "".equals(slots[1].eid), "empty slot exposes card identity");
        check(slots[1].supportedMepMode==MultipleEnabledProfilesMode.NONE, "empty slot has multiple profiles");
        slots[0].portInfo[0].logicalSlotId=1;
        slots[1].portInfo[0].logicalSlotId=0;
        s.getSimSlotsStatus(12);
        response(p,"getSimSlotsStatusResponse",12,RadioError.NONE);
        check(((SimSlotStatus[])p.value)[0].portInfo[0].logicalSlotId==0, "caller changed slot state");
        check(((SimSlotStatus[])p.value)[1].portInfo[0].logicalSlotId==-1, "caller mapped empty slot");
        s.getPhoneCapability(13);
        response(p,"getPhoneCapabilityResponse",13,RadioError.NONE);
        PhoneCapability cap=(PhoneCapability)p.value;
        check(Arrays.equals(cap.logicalModemIds,new byte[]{0}) && cap.maxActiveVoice==1, "incorrect voice capability");
        check(cap.maxActiveData==0 && cap.maxActiveInternetData==0 && !cap.isInternetLingeringSupported,
                "unimplemented data capability claimed");
        cap.logicalModemIds[0]=1;
        s.getPhoneCapability(14);
        response(p,"getPhoneCapabilityResponse",14,RadioError.NONE);
        check(Arrays.equals(((PhoneCapability)p.value).logicalModemIds,new byte[]{0}), "caller changed modem state");
        s.setNumOfLiveModems(15,(byte)2);
        response(p,"setNumOfLiveModemsResponse",15,RadioError.REQUEST_NOT_SUPPORTED);
        s.getNumOfLiveModems(16);
        response(p,"getNumOfLiveModemsResponse",16,RadioError.NONE);
        check(((Byte)p.value).byteValue()==1, "slot count changed");
        model.disconnect();
        s.getNumOfLiveModems(17);
        response(p,"getNumOfLiveModemsResponse",17,RadioError.RADIO_NOT_AVAILABLE);
        s.getSimSlotsStatus(18);
        response(p,"getSimSlotsStatusResponse",18,RadioError.RADIO_NOT_AVAILABLE);
        s.getPhoneCapability(19);
        response(p,"getPhoneCapabilityResponse",19,RadioError.RADIO_NOT_AVAILABLE);
        System.out.println("RADIO_CONFIG_SINGLE_SLOT_OK");
    }
}
