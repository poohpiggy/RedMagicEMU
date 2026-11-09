package app.aemu.stub;

import android.hardware.radio.RadioError;
import android.hardware.radio.RadioResponseInfo;
import android.hardware.radio.data.IRadioDataIndication;
import android.hardware.radio.data.IRadioDataResponse;
import android.hardware.radio.data.SetupDataCallResult;
import android.os.IBinder;
import java.lang.reflect.Proxy;

/** A synthetic slot never connects to or creates a real cellular data bearer. */
public final class RadioDataNoBearerTest {
    private static void check(boolean ok,String message) { if(!ok) throw new AssertionError(message); }
    private static IBinder peer(){return (IBinder)Proxy.newProxyInstance(IBinder.class.getClassLoader(),new Class<?>[]{IBinder.class},(p,m,a)->{
        switch(m.getName()) { case "linkToDeath": return null; case "unlinkToDeath": return true;
            case "isBinderAlive": return true; case "hashCode": return System.identityHashCode(p);
            case "equals": return p==a[0]; default: throw new UnsupportedOperationException(m.getName()); }
    });}
    public static void main(String[] args) throws Exception {
        SyntheticSingleSlot slot=SyntheticSingleSlot.forTests(); RadioDataService data=new RadioDataService("slot1",slot);
        IBinder rb=peer(), ib=peer(); final RadioResponseInfo[] info={null}; final Object[] result={null};
        IRadioDataResponse response=(IRadioDataResponse)Proxy.newProxyInstance(IRadioDataResponse.class.getClassLoader(),new Class<?>[]{IRadioDataResponse.class},(p,m,a)->{
            if(m.getName().equals("asBinder")) return rb;
            info[0]=(RadioResponseInfo)a[0]; result[0]=a.length>1?a[1]:null; return null;
        });
        IRadioDataIndication indication=(IRadioDataIndication)Proxy.newProxyInstance(IRadioDataIndication.class.getClassLoader(),new Class<?>[]{IRadioDataIndication.class},(p,m,a)->{
            if(m.getName().equals("asBinder")) return ib; throw new AssertionError("unexpected indication");
        });
        data.setResponseFunctions(response,indication);
        data.getDataCallList(1); check(info[0].serial==1 && info[0].error==RadioError.RADIO_NOT_AVAILABLE,"OFF radio appeared available");
        slot.setRadioPower(true); data.getDataCallList(2);
        check(info[0].serial==2 && info[0].error==RadioError.NONE && ((SetupDataCallResult[])result[0]).length==0,"ON did not report empty data list");
        data.setupDataCall(3,0,null,false,0,null,null,0,null,false);
        check(info[0].serial==3 && info[0].error==RadioError.REQUEST_NOT_SUPPORTED,"data call was allowed");
        slot.disconnect(); data.getDataCallList(4);
        check(info[0].serial==4 && info[0].error==RadioError.RADIO_NOT_AVAILABLE,"disconnected radio appeared available");
        System.out.println("RADIO_DATA_NO_BEARER_OK");
    }
}
