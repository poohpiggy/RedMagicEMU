package app.aemu.stub;
public final class EarlyHandoffRecoveryTest {
 static final class B implements EarlyHandoffRecovery.Backend {boolean active,present,presentUnknown,disconnected=true,free=true,restore=true;int restores;
  public boolean ethernetActive(){return active;}public boolean ethernetPresent(){if(presentUnknown)throw new IllegalStateException("unknown");return present;}public boolean wifiDisconnected(){return disconnected;}public boolean ifaceFree(){return free;}
  public boolean restoreAndConfirm(){restores++;return restore;}}
 static void check(boolean v,String m){if(!v)throw new AssertionError(m);}
 public static void main(String[] ignored){
  B b=new B();check(EarlyHandoffRecovery.recover(b)==EarlyHandoffRecovery.RESTORED && b.restores==1,"safe Ethernet recovery not attempted");
  b=new B();b.active=true;check(EarlyHandoffRecovery.recover(b)==EarlyHandoffRecovery.RETAINED && b.restores==0,"active Ethernet unnecessarily changed");
  b=new B();b.present=true;check(EarlyHandoffRecovery.recover(b)==EarlyHandoffRecovery.OBSERVED_PRESENT && b.restores==0,"old Ethernet observed but duplicate registration attempted");
  b=new B();b.presentUnknown=true;check(EarlyHandoffRecovery.recover(b)==EarlyHandoffRecovery.UNKNOWN && b.restores==0,"unreadable old network allowed restore");
  b=new B();b.disconnected=false;check(EarlyHandoffRecovery.recover(b)==EarlyHandoffRecovery.UNKNOWN && b.restores==0,"unknown Wi-Fi allowed restore");
  b=new B();b.free=false;check(EarlyHandoffRecovery.recover(b)==EarlyHandoffRecovery.UNKNOWN && b.restores==0,"occupied interface allowed restore");
  b=new B();b.restore=false;check(EarlyHandoffRecovery.recover(b)==EarlyHandoffRecovery.UNKNOWN,"unconfirmed restore reported success");
  System.out.println("PASS early handoff conservative recovery");
 }
}
