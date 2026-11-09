package app.aemu.stub;

import android.hardware.radio.RadioError;
import android.hardware.radio.RadioResponseInfo;
import android.hardware.radio.RadioTechnology;
import android.hardware.radio.network.AccessTechnologySpecificInfo;
import android.hardware.radio.network.CellIdentity;
import android.hardware.radio.network.IRadioNetworkIndication;
import android.hardware.radio.network.IRadioNetworkResponse;
import android.hardware.radio.network.RegState;
import android.hardware.radio.network.RegStateResult;
import android.os.IBinder;
import android.os.Parcel;
import java.lang.reflect.Proxy;

/** Synthetic LTE network and unavailable-state contract; never reads host network state. */
public final class RadioNetworkUnregisteredTest {
    private static void check(boolean ok, String reason) { if (!ok) throw new AssertionError(reason); }
    private static IBinder peer() {
        return (IBinder)Proxy.newProxyInstance(IBinder.class.getClassLoader(),new Class<?>[]{IBinder.class},(p,m,a)->{
            switch(m.getName()) {
                case "linkToDeath": return null; case "unlinkToDeath": return true;
                case "isBinderAlive": return true; case "hashCode": return System.identityHashCode(p);
                case "equals": return p==a[0]; default: throw new UnsupportedOperationException(m.getName());
            }
        });
    }
    private static void parcelRoundTrip(RegStateResult original) {
        Parcel parcel=Parcel.obtain();
        try {
            parcel.writeTypedObject(original,0); parcel.setDataPosition(0);
            RegStateResult copy=parcel.readTypedObject(RegStateResult.CREATOR);
            check(copy!=null && copy.regState==RegState.REG_HOME && copy.rat==RadioTechnology.LTE,"parcel changed registration state");
            check("46000".equals(copy.registeredPlmn) && copy.cellIdentity.getTag()==CellIdentity.noinit
                    && copy.accessTechnologySpecificInfo.getTag()==AccessTechnologySpecificInfo.noinit,"parcel changed noinit unions");
        } finally { parcel.recycle(); }
    }
    public static void main(String[] args) throws Exception {
        SyntheticSingleSlot model=SyntheticSingleSlot.forTests();
        RadioNetworkService service=new RadioNetworkService("slot1",model);
        IBinder rb=peer(),ib=peer(); final RadioResponseInfo[] info={null}; final Object[][] values={null};
        IRadioNetworkResponse response=(IRadioNetworkResponse)Proxy.newProxyInstance(IRadioNetworkResponse.class.getClassLoader(),new Class<?>[]{IRadioNetworkResponse.class},(p,m,a)->{
            if(m.getName().equals("asBinder")) return rb;
            if(a!=null && a.length>0 && a[0] instanceof RadioResponseInfo){info[0]=(RadioResponseInfo)a[0];values[0]=a;return null;}
            throw new AssertionError("unexpected response "+m.getName());
        });
        IRadioNetworkIndication indication=(IRadioNetworkIndication)Proxy.newProxyInstance(IRadioNetworkIndication.class.getClassLoader(),new Class<?>[]{IRadioNetworkIndication.class},(p,m,a)->{
            if(m.getName().equals("asBinder")) return ib; throw new AssertionError("unexpected indication");
        });
        service.setResponseFunctions(response,indication);
        service.getVoiceRegistrationState(701);
        check(info[0].serial==701 && info[0].error==RadioError.RADIO_NOT_AVAILABLE,"OFF voice registration did not fail");
        model.setRadioPower(true);
        service.getVoiceRegistrationState(702);
        check(info[0].serial==702 && info[0].error==RadioError.NONE,"ON voice query failed");
        RegStateResult voice=(RegStateResult)values[0][1];
        check(voice.regState==RegState.REG_HOME && voice.rat==RadioTechnology.LTE,"voice LTE registration missing");
        check("46000".equals(voice.registeredPlmn),"wrong synthetic PLMN");
        check(voice.cellIdentity.getTag()==CellIdentity.noinit && voice.accessTechnologySpecificInfo.getTag()==AccessTechnologySpecificInfo.noinit,"invalid noinit union");
        service.getDataRegistrationState(703);
        check(info[0].serial==703 && info[0].error==RadioError.NONE,"ON data query failed");
        RegStateResult data=(RegStateResult)values[0][1];
        check(data.regState==RegState.REG_HOME && data.rat==RadioTechnology.LTE && "46000".equals(data.registeredPlmn),"data LTE registration missing");
        check(data.cellIdentity.getTag()==CellIdentity.noinit && data.accessTechnologySpecificInfo.getTag()==AccessTechnologySpecificInfo.noinit,"invalid data unions");
        if (args.length==1 && args[0].equals("parcel")) {
            parcelRoundTrip(voice); parcelRoundTrip(data);
            System.out.println("RADIO_NETWORK_PARCEL_OK");
        } else if (args.length!=0) throw new IllegalArgumentException("unknown mode");
        service.getOperator(704);
        check(info[0].serial==704 && info[0].error==RadioError.NONE && "RedMagicEMU".equals(values[0][1]) && "RedMagicEMU".equals(values[0][2]) && "46000".equals(values[0][3]),"synthetic operator incorrect");
        check(model.isNetworkRegistered() && model.isSimReady() && model.canDeliverVoiceOrSms(),"virtual network or synthetic card unavailable");
        model.setRadioPower(false);
        service.getDataRegistrationState(706);
        check(info[0].serial==706 && info[0].error==RadioError.RADIO_NOT_AVAILABLE && !model.isNetworkRegistered(),"powered off network remained available");
        service.getOperator(707);
        check(info[0].serial==707 && info[0].error==RadioError.RADIO_NOT_AVAILABLE && "".equals(values[0][1]) && "".equals(values[0][2]) && "".equals(values[0][3]),"powered off operator remained available");
        model.setRadioPower(true);
        service.getVoiceRegistrationState(708);
        check(info[0].serial==708 && info[0].error==RadioError.NONE && ((RegStateResult)values[0][1]).regState==RegState.REG_HOME,"network did not return after power on");
        model.disconnect(); service.getDataRegistrationState(705);
        check(info[0].serial==705 && info[0].error==RadioError.RADIO_NOT_AVAILABLE,"disconnected data query succeeded");
        System.out.println("RADIO_NETWORK_UNREGISTERED_OK");
    }
}
