package app.aemu.stub;

import android.hardware.radio.RadioError;
import android.hardware.radio.RadioResponseInfo;
import android.hardware.radio.sim.AppStatus;
import android.hardware.radio.sim.CardStatus;
import android.hardware.radio.sim.IccIo;
import android.hardware.radio.sim.IccIoResult;
import android.hardware.radio.sim.IRadioSimIndication;
import android.hardware.radio.sim.IRadioSimResponse;
import android.os.IBinder;
import java.lang.reflect.Proxy;

/** Synthetic USIM application state and supported profile reads. */
public final class RadioSimApplicationTest {
    private static void check(boolean value,String reason){if(!value)throw new AssertionError(reason);}
    private static IBinder peer(){return (IBinder)Proxy.newProxyInstance(IBinder.class.getClassLoader(),new Class<?>[]{IBinder.class},(p,m,a)->{
        switch(m.getName()){
            case "linkToDeath":return null; case "unlinkToDeath":return true; case "isBinderAlive":return true;
            case "hashCode":return System.identityHashCode(p); case "equals":return p==a[0];
            default:throw new UnsupportedOperationException(m.getName());
        }
    });}
    private static IccIo io(int command,int file,String path,String aid,int size){
        IccIo io=new IccIo();io.command=command;io.fileId=file;io.path=path;io.aid=aid;io.p3=size;return io;
    }
    public static void main(String[] args)throws Exception{
        SyntheticSingleSlot model=SyntheticSingleSlot.forTests();RadioSimService sim=new RadioSimService("slot1",model);
        final RadioResponseInfo[] info={null};final Object[] data={null};final String[] method={null};IBinder rb=peer(),ib=peer();
        IRadioSimResponse response=(IRadioSimResponse)Proxy.newProxyInstance(IRadioSimResponse.class.getClassLoader(),new Class<?>[]{IRadioSimResponse.class},(p,m,a)->{
            if(m.getName().equals("asBinder"))return rb;
            if(a!=null && a.length>1 && a[0] instanceof RadioResponseInfo){info[0]=(RadioResponseInfo)a[0];data[0]=a[1];method[0]=m.getName();return null;}
            throw new AssertionError("unexpected response "+m.getName());
        });
        IRadioSimIndication indication=(IRadioSimIndication)Proxy.newProxyInstance(IRadioSimIndication.class.getClassLoader(),new Class<?>[]{IRadioSimIndication.class},(p,m,a)->{
            if(m.getName().equals("asBinder"))return ib;throw new AssertionError("unexpected indication");
        });
        sim.setResponseFunctions(response,indication);sim.getIccCardStatus(801);
        check(info[0].serial==801 && info[0].error==RadioError.NONE,"card status failed");
        CardStatus card=(CardStatus)data[0];
        check(card.applications.length==1 && card.gsmUmtsSubscriptionAppIndex==0,"single USIM application missing");
        AppStatus app=card.applications[0];
        check(app.appType==AppStatus.APP_TYPE_USIM && app.appState==AppStatus.APP_STATE_READY,"synthetic USIM application not ready");
        check(app.pin1==android.hardware.radio.sim.PinState.DISABLED,"synthetic USIM PIN1 not disabled");
        String aid=app.aidPtr;check(aid.equals(model.simProfile().aid()),"card and profile AID differ");
        sim.getImsiForApp(802,aid);
        check(info[0].serial==802 && info[0].error==RadioError.NONE && model.simProfile().imsi().equals(data[0]),"synthetic IMSI failed");
        sim.getImsiForApp(803,"A0000000871002");
        check(info[0].serial==803 && info[0].error==RadioError.REQUEST_NOT_SUPPORTED,"unknown AID accepted for IMSI");
        sim.iccIoForApp(804,io(0xc0,0x6fad,"3F007FFF",aid,15));
        check(info[0].serial==804 && info[0].error==RadioError.NONE && "000000040000040000000000000000".equals(((IccIoResult)data[0]).simResponse),"EF_AD metadata failed");
        sim.iccIoForApp(805,io(0xb0,0x6fad,"3F007FFF",aid,4));
        check(info[0].serial==805 && info[0].error==RadioError.NONE && "00000002".equals(((IccIoResult)data[0]).simResponse),"EF_AD MNC length failed");
        sim.iccIoForApp(806,io(0xb0,0x2fe2,"3F00",aid,10));
        check(info[0].serial==806 && info[0].error==RadioError.NONE && "98000000000000000040".equals(((IccIoResult)data[0]).simResponse),"application ICCID failed");
        sim.iccIoForApp(807,io(0xb0,0x6fad,"3F007FFF","A0000000871002",4));
        check(info[0].serial==807 && info[0].error==RadioError.REQUEST_NOT_SUPPORTED,"wrong application accessed EF_AD");
        // AOSP USIM reads EF_SPN as a transparent file under the selected ADF.
        // The first byte is display condition, followed by the synthetic SPN.
        sim.iccIoForApp(809,io(0xc0,0x6f46,"3F007FFF",aid,15));
        check(info[0].serial==809 && info[0].error==RadioError.NONE && "000000110000040000000000000000".equals(((IccIoResult)data[0]).simResponse),"EF_SPN metadata failed");
        sim.iccIoForApp(810,io(0xb0,0x6f46,"3F007FFF",aid,17));
        check(info[0].serial==810 && info[0].error==RadioError.NONE && "005265644D61676963454D55FFFFFFFFFF".equals(((IccIoResult)data[0]).simResponse),"EF_SPN data failed");
        sim.iccIoForApp(811,io(0xb0,0x6f46,"3F007FFF","A0000000871002",17));
        check(info[0].serial==811 && info[0].error==RadioError.REQUEST_NOT_SUPPORTED,"wrong application accessed EF_SPN");
        sim.iccIoForApp(812,io(0xb0,0x6f46,"3F00",aid,17));
        check(info[0].serial==812 && info[0].error==RadioError.REQUEST_NOT_SUPPORTED,"wrong path accessed EF_SPN");
        sim.iccIoForApp(813,io(0xb0,0x6f46,"3F007FFF",aid,16));
        check(info[0].serial==813 && info[0].error==RadioError.NONE
                && "005265644D61676963454D55FFFFFFFF".equals(((IccIoResult)data[0]).simResponse),
                "valid partial EF_SPN read failed");
        IccIo pastEnd=io(0xb0,0x6f46,"3F007FFF",aid,18);
        sim.iccIoForApp(815,pastEnd);
        check(info[0].serial==815 && info[0].error==RadioError.REQUEST_NOT_SUPPORTED,
                "out-of-bounds EF_SPN read accepted");
        // A USIM service table must not advertise services the prototype cannot provide.
        sim.iccIoForApp(816,io(0xc0,0x6f38,"3F007FFF",aid,15));
        check(info[0].serial==816 && info[0].error==RadioError.NONE && "000000040000040000000000000000".equals(((IccIoResult)data[0]).simResponse),"EF_UST metadata failed");
        sim.iccIoForApp(817,io(0xb0,0x6f38,"3F007FFF",aid,4));
        check(info[0].serial==817 && info[0].error==RadioError.NONE && "00000400".equals(((IccIoResult)data[0]).simResponse),"EF_UST data failed");
        check("iccIoForAppResponse".equals(method[0]) && ((IccIoResult)data[0]).sw1==0x90 && ((IccIoResult)data[0]).sw2==0,"EF_UST response type or status failed");
        sim.iccIoForApp(818,io(0xb0,0x6f38,"3F007FFF","A0000000871002",4));
        check(info[0].serial==818 && info[0].error==RadioError.REQUEST_NOT_SUPPORTED,"wrong application accessed EF_UST");
        sim.iccIoForApp(819,io(0xb0,0x6f38,"3F00",aid,4));
        check(info[0].serial==819 && info[0].error==RadioError.REQUEST_NOT_SUPPORTED,"wrong path accessed EF_UST");
        sim.iccIoForApp(820,io(0xb0,0x6f38,"3F007FFF",aid,5));
        check(info[0].serial==820 && info[0].error==RadioError.REQUEST_NOT_SUPPORTED,"out-of-bounds EF_UST read accepted");
        sim.iccIoForApp(822,io(0xb0,0x6f38,"3F007FFF",aid,3));
        check(info[0].serial==822 && info[0].error==RadioError.NONE && "000004".equals(((IccIoResult)data[0]).simResponse),"EF_UST prefix read failed");
        sim.iccIoForApp(823,io(0xd6,0x6f38,"3F007FFF",aid,4));
        check(info[0].serial==823 && info[0].error==RadioError.REQUEST_NOT_SUPPORTED,"EF_UST write accepted");
        check(model.isSimReady() && !model.isNetworkRegistered() && !model.canDeliverVoiceOrSms(),"powered off synthetic USIM state incorrect");
        model.disconnect();sim.getImsiForApp(808,aid);
        check(info[0].serial==808 && info[0].error==RadioError.RADIO_NOT_AVAILABLE,"disconnected IMSI read accepted");
        sim.iccIoForApp(821,io(0xb0,0x6f38,"3F007FFF",aid,4));
        check(info[0].serial==821 && info[0].error==RadioError.RADIO_NOT_AVAILABLE,"disconnected EF_UST read accepted");
        System.out.println("RADIO_SIM_APPLICATION_OK");
    }
}
