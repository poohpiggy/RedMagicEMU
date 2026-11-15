package app.aemu.stub;
/** On uncertain handoff, restore only after the Wi-Fi lifecycle and interface are independently known safe. */
final class EarlyHandoffRecovery {
 static final int RETAINED=0,RESTORED=1,UNKNOWN=2,OBSERVED_PRESENT=3;
 interface Backend {boolean ethernetActive()throws Exception;boolean ethernetPresent()throws Exception;boolean wifiDisconnected()throws Exception;boolean ifaceFree()throws Exception;boolean restoreAndConfirm()throws Exception;}
 static int recover(Backend backend){try{if(backend.ethernetActive())return RETAINED;if(backend.ethernetPresent())return OBSERVED_PRESENT;if(!backend.wifiDisconnected()||!backend.ifaceFree())return UNKNOWN;return backend.restoreAndConfirm()?RESTORED:UNKNOWN;}catch(Exception failure){return UNKNOWN;}}
}
