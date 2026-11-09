package app.aemu.stub;

import android.os.IBinder;
import java.lang.reflect.Constructor;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/** Host-only publication contract; no actual services are registered. */
public final class RadioSlot1RegistryTest {
    private static final String[] EXPECTED = {
        "android.hardware.radio.config.IRadioConfig/default",
        "android.hardware.radio.modem.IRadioModem/slot1",
        "android.hardware.radio.sim.IRadioSim/slot1",
        "android.hardware.radio.network.IRadioNetwork/slot1",
        "android.hardware.radio.data.IRadioData/slot1",
        "android.hardware.radio.messaging.IRadioMessaging/slot1",
        "android.hardware.radio.voice.IRadioVoice/slot1"
    };
    private static void check(boolean ok, String why) { if (!ok) throw new AssertionError(why); }
    private static IBinder peer() {
        return (IBinder)Proxy.newProxyInstance(IBinder.class.getClassLoader(),new Class<?>[]{IBinder.class},(p,m,a)-> {
            if (m.getName().equals("hashCode")) return System.identityHashCode(p);
            if (m.getName().equals("equals")) return p==a[0];
            throw new UnsupportedOperationException(m.getName());
        });
    }
    private static final class Backend {
        final List<String> events=new ArrayList<>(); int missing=-1, occupied=-1, failAdd=-1, missingPublished=-1;
        Object proxy(Class<?> type) {
            return Proxy.newProxyInstance(type.getClassLoader(),new Class<?>[]{type},(p,m,a)-> {
                String name=(String)a[0]; int index=Arrays.asList(EXPECTED).indexOf(name);
                check(index>=0,"unexpected service name "+name);
                events.add(m.getName()+":"+name);
                if (m.getName().equals("isDeclared")) return index!=missing;
                if (m.getName().equals("isOccupied")) return index==occupied;
                if (m.getName().equals("isPublished")) return index!=missingPublished;
                if (m.getName().equals("add")) {
                    if (index==failAdd) throw new IllegalStateException("test add failure");
                    return null;
                }
                throw new AssertionError("unexpected backend operation "+m.getName());
            });
        }
        int count(String prefix) { int count=0; for (String event:events) if(event.startsWith(prefix+":"))count++; return count; }
    }
    private static Throwable failure(Method method,Object target) throws Exception {
        try { method.invoke(target); throw new AssertionError("start unexpectedly succeeded"); }
        catch (InvocationTargetException expected) { return expected.getCause(); }
    }
    public static void main(String[] args) throws Exception {
        final Class<?> registry,backendType;
        try { registry=Class.forName("app.aemu.stub.RadioSlot1Registry"); backendType=Class.forName("app.aemu.stub.RadioSlot1Registry$Backend"); }
        catch(ClassNotFoundException missing) { throw new AssertionError("formal slot1 registry missing",missing); }
        Constructor<?> constructor=registry.getDeclaredConstructor(backendType,IBinder[].class); constructor.setAccessible(true);
        Method start=registry.getDeclaredMethod("start"); start.setAccessible(true);
        Method exit=registry.getDeclaredMethod("mustExitProcess"); exit.setAccessible(true);
        IBinder[] binders=new IBinder[7]; for(int i=0;i<binders.length;i++)binders[i]=peer();
        Backend good=new Backend(); Object service=constructor.newInstance(good.proxy(backendType),binders);
        start.invoke(service);
        check(good.count("isDeclared")==7 && good.count("isOccupied")==7 && good.count("add")==7 && good.count("isPublished")==7,"not all seven services checked, submitted and observed");
        for(int i=0;i<7;i++) check(good.events.get(14+i).equals("add:"+EXPECTED[i]),"wrong registration order/name");
        check((Boolean)exit.invoke(service),"published process not marked for exit on teardown");
        check(failure(start,service) instanceof IllegalStateException,"second start accepted");
        for(int kind=0;kind<4;kind++) {
            Backend bad=new Backend(); if(kind==0)bad.missing=3; if(kind==1)bad.occupied=4; if(kind==2)bad.failAdd=2; if(kind==3)bad.missingPublished=4;
            Object failed=constructor.newInstance(bad.proxy(backendType),binders);
            check(failure(start,failed)!=null,"failure hidden");
            check((Boolean)exit.invoke(failed),"failed start did not require exit");
            check(bad.count("add")== (kind==2 ? 3 : kind==3 ? 7 : 0),"publication occurred after failed precheck or wrong partial count");
        }
        IBinder[] duplicate=binders.clone(); duplicate[6]=duplicate[0];
        try { constructor.newInstance(good.proxy(backendType),duplicate); throw new AssertionError("duplicate binder accepted"); }
        catch(InvocationTargetException expected) { check(expected.getCause() instanceof IllegalArgumentException,"wrong duplicate error"); }
        Class<?> entry=Class.forName("app.aemu.stub.RadioServiceMain");
        Method create;
        try { create=entry.getDeclaredMethod("createServiceGroup",SyntheticSingleSlot.class); }
        catch (NoSuchMethodException missing) { throw new AssertionError("formal shared service group missing",missing); }
        create.setAccessible(true);
        SyntheticSingleSlot model=SyntheticSingleSlot.forTests();
        Object group=create.invoke(null,model);
        java.lang.reflect.Field servicesField=group.getClass().getDeclaredField("services"); servicesField.setAccessible(true);
        java.lang.reflect.Field endpointField=group.getClass().getDeclaredField("endpoint"); endpointField.setAccessible(true);
        IBinder[] actual=(IBinder[])servicesField.get(group);
        RadioEventEndpoint endpoint=(RadioEventEndpoint)endpointField.get(group);
        String[] expectedClasses={"RadioConfigService","RadioModemService","RadioSimService","RadioNetworkService","RadioDataService","RadioMessagingService","RadioVoiceService"};
        check(actual.length==7,"wrong service count");
        try {
            for(int i=0;i<actual.length;i++) {
                check(actual[i].getClass().getSimpleName().equals(expectedClasses[i]),"service/name order mismatch");
                java.lang.reflect.Field field=null;
                for (java.lang.reflect.Field candidate:actual[i].getClass().getDeclaredFields())
                    if (candidate.getType()==SyntheticSingleSlot.class) { field=candidate; break; }
                check(field!=null,"service is missing shared state"); field.setAccessible(true);
                check(field.get(actual[i])==model,"services do not share one synthetic slot");
            }
            check(endpoint!=null,"formal event endpoint missing");
            for (String fieldName:new String[]{"slot","messaging","voice"}) {
                java.lang.reflect.Field field=RadioEventEndpoint.class.getDeclaredField(fieldName); field.setAccessible(true);
                Object expected=fieldName.equals("slot") ? model : fieldName.equals("messaging") ? actual[5] : actual[6];
                check(field.get(endpoint)==expected,"endpoint does not use registered "+fieldName);
            }
            model.setRadioPower(true);
            check(endpoint.dispatch(1,new byte[]{0,4,11})==RadioEventEndpoint.Outcome.UNCERTAIN,"unbound messaging callback falsely submitted SMS");
            check(endpoint.dispatch(2,new byte[]{'1','0','0','8','6'})==RadioEventEndpoint.Outcome.UNCERTAIN,"unbound voice callback falsely submitted call");
            try { new RadioEventEndpoint(model,(RadioVoiceService)actual[6],(RadioMessagingService)actual[5]);
                throw new AssertionError("duplicate endpoint accepted"); }
            catch (IllegalStateException expected) { }
            model.setRadioPower(false);
            check(model.isSimReady() && !model.isNetworkRegistered() && !model.canDeliverVoiceOrSms(),"powered off radio allowed delivery");
        } finally {
            ((RadioVoiceService)actual[6]).close(); ((RadioModemService)actual[1]).detachStateListener(); model.disconnect();
        }
        System.out.println("RADIO_SLOT1_REGISTRY_OK");
    }
}
