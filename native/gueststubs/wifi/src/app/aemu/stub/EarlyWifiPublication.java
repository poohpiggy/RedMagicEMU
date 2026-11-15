package app.aemu.stub;
/** Diagnostic-only publication, with no Ethernet or network-control operation. */
final class EarlyWifiPublication {
 interface Backend {
  boolean preparing();boolean connected();Object original()throws Exception;boolean alive(Object binder)throws Exception;
  Object current()throws Exception;boolean isOurService(Object binder)throws Exception;void publish()throws Exception;
  boolean confirm()throws Exception;void log(String message);
 }
 static boolean publish(Backend b){
  boolean attempted=false;
  try{
   if(!b.preparing() || b.connected()){b.log("early_abort_invalid_preparation");return false;}
   Object candidate=b.original();
   if(candidate==null || !b.alive(candidate)){b.log("early_abort_original_candidate_unavailable");return false;}
   if(b.isOurService(candidate)){b.log("early_abort_candidate_already_virtual");return false;}
   Object current=b.current();
   if(current==null || !candidate.equals(current) || !b.alive(current) || b.isOurService(current)){
    b.log("early_abort_wifi_owner_changed_or_unknown");return false;
   }
   b.log("early_publish_check_add_not_atomic_race_possible");attempted=true;b.publish();
   if(!b.confirm()){b.log("early_publish_outcome_unknown");return false;}
   b.log("early_virtual_wifi_confirmed_no_ethernet_handoff");return true;
  }catch(Exception e){b.log((attempted?"early_publish_outcome_unknown_":"early_abort_")+e.getClass().getName());return false;}
 }
}
