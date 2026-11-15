package app.aemu.stub;
public final class EarlyPublicationTest {
 static class B implements EarlyWifiPublication.Backend {
  Object original=new Object(),current=original,ours=new Object();boolean ready=true,connected=false,alive=true,alreadyVirtual=false,confirm=true,throwPublish=false;int publishes,stops;
  public boolean preparing(){return ready;} public boolean connected(){return connected;}public Object original(){return original;}public boolean alive(Object x){return alive;}public Object current(){return current;}
  public boolean isOurService(Object x){return x==ours || (x==original && alreadyVirtual);}public void publish(){publishes++;if(throwPublish)throw new IllegalStateException();current=ours;}
  public boolean confirm(){return confirm;}public void log(String s){}
 }
 static void check(boolean v,String x){if(!v)throw new AssertionError(x);}
 public static void main(String[] args){
  B b=new B();check(EarlyWifiPublication.publish(b),"early publish failed");check(b.publishes==1 && b.stops==0,"unexpected mutation");
  b=new B();b.connected=true;check(!EarlyWifiPublication.publish(b)&&b.publishes==0&&b.stops==0,"preparation reports connected");
  b=new B();b.alive=false;check(!EarlyWifiPublication.publish(b)&&b.publishes==0&&b.stops==0,"dead candidate published");
  b=new B();b.current=new Object();check(!EarlyWifiPublication.publish(b)&&b.publishes==0&&b.stops==0,"changed owner overwritten");
  b=new B();b.alreadyVirtual=true;check(!EarlyWifiPublication.publish(b)&&b.publishes==0,"existing virtual candidate overwritten");
  b=new B();b.confirm=false;check(!EarlyWifiPublication.publish(b)&&b.publishes==1&&b.stops==0,"unconfirmed outcome treated as success");
  b=new B();b.throwPublish=true;check(!EarlyWifiPublication.publish(b)&&b.stops==0,"publication exception masked");
  System.out.println("PASS early publication failures preserve Ethernet");
 }
}
