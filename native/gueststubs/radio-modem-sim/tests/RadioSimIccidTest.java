package app.aemu.stub;

import android.hardware.radio.RadioError;
import android.hardware.radio.RadioResponseInfo;
import android.hardware.radio.sim.IccIo;
import android.hardware.radio.sim.IccIoResult;
import android.hardware.radio.sim.IRadioSimIndication;
import android.hardware.radio.sim.IRadioSimResponse;
import android.os.IBinder;
import java.lang.reflect.Proxy;

/** Only the synthetic master-file ICCID protocol, not a READY SIM or subscription test. */
public final class RadioSimIccidTest {
    private static void check(boolean value, String reason) { if (!value) throw new AssertionError(reason); }
    private static IBinder peer() {
        return (IBinder) Proxy.newProxyInstance(IBinder.class.getClassLoader(),new Class<?>[]{IBinder.class},(p,m,a) -> {
            switch(m.getName()) {
                case "linkToDeath": return null; case "unlinkToDeath": return true;
                case "isBinderAlive": return true; case "hashCode": return System.identityHashCode(p);
                case "equals": return p==a[0]; default: throw new UnsupportedOperationException(m.getName());
            }
        });
    }
    private static IccIo request(int command, int size) {
        IccIo io=new IccIo(); io.command=command; io.fileId=0x2fe2; io.path="3F00"; io.p3=size; return io;
    }
    public static void main(String[] args) throws Exception {
        SyntheticSingleSlot model=SyntheticSingleSlot.forTests();
        RadioSimService sim=new RadioSimService("slot1",model);
        IBinder rb=peer(),ib=peer(); final RadioResponseInfo[] info={null}; final IccIoResult[] data={null};
        IRadioSimResponse response=(IRadioSimResponse)Proxy.newProxyInstance(IRadioSimResponse.class.getClassLoader(),new Class<?>[]{IRadioSimResponse.class},(p,m,a)->{
            if (m.getName().equals("asBinder")) return rb;
            if (m.getName().equals("iccIoForAppResponse")) { info[0]=(RadioResponseInfo)a[0]; data[0]=(IccIoResult)a[1]; return null; }
            throw new AssertionError("unexpected callback "+m.getName());
        });
        IRadioSimIndication indication=(IRadioSimIndication)Proxy.newProxyInstance(IRadioSimIndication.class.getClassLoader(),new Class<?>[]{IRadioSimIndication.class},(p,m,a)->{
            if (m.getName().equals("asBinder")) return ib; throw new AssertionError("unexpected indication");
        });
        sim.setResponseFunctions(response,indication);
        sim.iccIoForApp(601,request(0xc0,15));
        check(info[0].serial==601 && info[0].error==RadioError.NONE,"GET_RESPONSE failed");
        check(data[0].sw1==0x90 && data[0].sw2==0 && "0000000A0000040000000000000000".equals(data[0].simResponse),"invalid transparent EF metadata");
        sim.iccIoForApp(602,request(0xb0,10));
        check(info[0].serial==602 && info[0].error==RadioError.NONE,"READ_BINARY failed");
        check(data[0].sw1==0x90 && data[0].sw2==0 && "98000000000000000040".equals(data[0].simResponse),"ICCID data does not match profile");
        IccIo wrong=request(0xb0,10); wrong.path="3F007FFF";
        sim.iccIoForApp(603,wrong);
        check(info[0].serial==603 && info[0].error==RadioError.REQUEST_NOT_SUPPORTED,"wrong path accepted");
        wrong=request(0xb0,10); wrong.p1=1;
        sim.iccIoForApp(604,wrong);
        check(info[0].serial==604 && info[0].error==RadioError.REQUEST_NOT_SUPPORTED,"unsupported offset accepted");
        wrong=request(0xb0,11);
        sim.iccIoForApp(605,wrong);
        check(info[0].serial==605 && info[0].error==RadioError.REQUEST_NOT_SUPPORTED,"unsupported read length accepted");
        sim.iccIoForApp(611,request(0xb0,1));
        check(info[0].serial==611 && info[0].error==RadioError.NONE && "98".equals(data[0].simResponse),"ICCID prefix failed");
        sim.iccIoForApp(612,request(0xb0,0));
        check(info[0].serial==612 && info[0].error==RadioError.REQUEST_NOT_SUPPORTED,"zero length accepted");
        sim.iccIoForApp(613,request(0xb0,-1));
        check(info[0].serial==613 && info[0].error==RadioError.REQUEST_NOT_SUPPORTED,"negative length accepted");
        wrong=request(0xb0,10); wrong.aid="A0000000871002";
        sim.iccIoForApp(606,wrong);
        check(info[0].serial==606 && info[0].error==RadioError.REQUEST_NOT_SUPPORTED,"unannounced SIM application accepted");
        wrong=request(0xc0,15); wrong.fileId=0x6fad;
        sim.iccIoForApp(608,wrong);
        check(info[0].serial==608 && info[0].error==RadioError.REQUEST_NOT_SUPPORTED,"unimplemented EF accepted");
        wrong=request(0xd6,10);
        sim.iccIoForApp(609,wrong);
        check(info[0].serial==609 && info[0].error==RadioError.REQUEST_NOT_SUPPORTED,"write command accepted");
        wrong=request(0xb0,10); wrong.pin2="0000";
        sim.iccIoForApp(610,wrong);
        check(info[0].serial==610 && info[0].error==RadioError.REQUEST_NOT_SUPPORTED,"PIN-bearing command accepted");
        model.disconnect(); sim.iccIoForApp(607,request(0xb0,10));
        check(info[0].serial==607 && info[0].error==RadioError.RADIO_NOT_AVAILABLE,"disconnected SIM read accepted");
        check(!model.isSimReady() && !model.isNetworkRegistered(),"ICCID read marked model READY");
        System.out.println("RADIO_SIM_ICCID_OK");
    }
}
