package app.aemu.stub;

import android.hardware.radio.RadioError;
import android.hardware.radio.RadioResponseInfo;
import android.hardware.radio.data.IRadioDataIndication;
import android.hardware.radio.data.IRadioDataResponse;
import android.hardware.radio.data.Qos;
import android.hardware.radio.data.SetupDataCallResult;
import android.hardware.radio.data.SlicingConfig;
import android.os.IBinder;
import android.os.Parcel;
import java.lang.reflect.Proxy;

/** Run only in Guest: ensure unsupported response values survive target Parcel serialization. */
public final class RadioDataGuestParcelTest {
    private static void check(boolean ok,String message) { if(!ok) throw new AssertionError(message); }
    private static IBinder peer() {
        return (IBinder)Proxy.newProxyInstance(IBinder.class.getClassLoader(),new Class<?>[]{IBinder.class},(p,m,a)->{
            switch(m.getName()) {
                case "linkToDeath": return null; case "unlinkToDeath": return true; case "isBinderAlive": return true;
                case "hashCode": return System.identityHashCode(p); case "equals": return p==a[0];
                default: throw new UnsupportedOperationException(m.getName());
            }
        });
    }
    public static void main(String[] args) throws Exception {
        RadioDataService service=new RadioDataService("slot1"); IBinder rb=peer(),ib=peer();
        final RadioResponseInfo[] info={null}; final Object[] value={null};
        IRadioDataResponse response=(IRadioDataResponse)Proxy.newProxyInstance(IRadioDataResponse.class.getClassLoader(),new Class<?>[]{IRadioDataResponse.class},(p,m,a)->{
            if(m.getName().equals("asBinder")) return rb;
            info[0]=(RadioResponseInfo)a[0]; value[0]=a.length>1?a[1]:null; return null;
        });
        IRadioDataIndication indication=(IRadioDataIndication)Proxy.newProxyInstance(IRadioDataIndication.class.getClassLoader(),new Class<?>[]{IRadioDataIndication.class},(p,m,a)->{
            if(m.getName().equals("asBinder")) return ib; throw new AssertionError("unexpected indication");
        });
        service.setResponseFunctions(response,indication);
        service.setupDataCall(11,0,null,false,0,null,null,0,null,false);
        check(info[0].serial==11 && info[0].error==RadioError.REQUEST_NOT_SUPPORTED,"unexpected setup result");
        Parcel parcel=Parcel.obtain();
        try {
            ((SetupDataCallResult)value[0]).writeToParcel(parcel,0); parcel.setDataPosition(0);
            SetupDataCallResult decoded=SetupDataCallResult.CREATOR.createFromParcel(parcel);
            check(decoded.defaultQos!=null && decoded.defaultQos.getTag()==Qos.noinit,"invalid QoS union");
            check(decoded.addresses!=null && decoded.addresses.length==0 && decoded.ifname.isEmpty(),"unexpected bearer");
        } finally { parcel.recycle(); }
        service.getSlicingConfig(12);
        check(info[0].serial==12 && info[0].error==RadioError.REQUEST_NOT_SUPPORTED,"unexpected slicing result");
        parcel=Parcel.obtain();
        try {
            ((SlicingConfig)value[0]).writeToParcel(parcel,0); parcel.setDataPosition(0);
            SlicingConfig decoded=SlicingConfig.CREATOR.createFromParcel(parcel);
            check(decoded.urspRules!=null && decoded.urspRules.length==0 && decoded.sliceInfo!=null && decoded.sliceInfo.length==0,"invalid slicing arrays");
        } finally { parcel.recycle(); }
        System.out.println("RADIO_DATA_GUEST_PARCEL_OK");
    }
}
