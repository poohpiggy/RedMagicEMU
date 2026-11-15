"""Exercise WifiService's actual transition with a failing persistence backend."""
from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).resolve().parents[1] / 'src/app/aemu/stub/WifiService.java').read_text(encoding='utf-8')
signature = 'private boolean transition(boolean nextEnabled, boolean nextSaved, boolean nextDown) {'
start = source.index(signature)
pos = start + len(signature)
depth = 1
while depth:
    if source[pos] == '{': depth += 1
    elif source[pos] == '}': depth -= 1
    pos += 1
method = source[start:pos]
airplane_signature = 'boolean disableForAirplaneMode() {'
if airplane_signature in source:
    airplane_start = source.index(airplane_signature)
    airplane_pos = airplane_start + len(airplane_signature)
    airplane_depth = 1
    while airplane_depth:
        if source[airplane_pos] == '{': airplane_depth += 1
        elif source[airplane_pos] == '}': airplane_depth -= 1
        airplane_pos += 1
    airplane_method = source[airplane_start:airplane_pos]
else:
    airplane_method = 'boolean disableForAirplaneMode() { return false; }'
info_signature = 'Object wifiInfo() {'
info_start = source.index(info_signature)
info_pos = info_start + len(info_signature)
info_depth = 1
while info_depth:
    if source[info_pos] == '{': info_depth += 1
    elif source[info_pos] == '}': info_depth -= 1
    info_pos += 1
info_method = source[info_start:info_pos]
constructor_start = source.index('this.stateStore = stateStore;')
constructor_end = source.index('Class<?> stub =', constructor_start)
constructor_init = source[constructor_start:constructor_end]
prepare_signature = 'boolean finishPreparation(StartupConfirmation confirmation) {'
if prepare_signature in source:
    prepare_start = source.index(prepare_signature)
    prepare_pos = prepare_start + len(prepare_signature)
    prepare_depth = 1
    while prepare_depth:
        if source[prepare_pos] == '{': prepare_depth += 1
        elif source[prepare_pos] == '}': prepare_depth -= 1
        prepare_pos += 1
    prepare_method = source[prepare_start:prepare_pos]
else:
    old_signature = 'boolean finishPreparation() {'
    if old_signature in source:
        old_start = source.index(old_signature)
        old_pos = old_start + len(old_signature)
        old_depth = 1
        while old_depth:
            if source[old_pos] == '{': old_depth += 1
            elif source[old_pos] == '}': old_depth -= 1
            old_pos += 1
        prepare_method = source[old_start:old_pos] + '\nboolean finishPreparation(StartupConfirmation ignored) { return finishPreparation(); }'
    else:
        prepare_method = 'boolean finishPreparation(StartupConfirmation ignored) { return false; }'

fixture = r'''
package app.aemu.stub;
import java.io.IOException;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
final class WifiPersistentState {
    static final class DurabilityException extends IOException {
        DurabilityException() { super("injected directory sync failure"); }
    }
    static final class Snapshot {
        final boolean enabled, saved;
        Snapshot(boolean enabled, boolean saved) { this.enabled=enabled; this.saved=saved; }
    }
    boolean enabled=true, saved=true, fail, failRuntime, failAfterWrite, failRead, failDurability;
    boolean block;
    final CountDownLatch writing = new CountDownLatch(1), releaseWrite = new CountDownLatch(1);
    Snapshot read() throws IOException {
        if (failRead) throw new IOException("injected read failure");
        return new Snapshot(enabled, saved);
    }
    void write(boolean enabled, boolean saved) throws IOException {
        if (block) {
            writing.countDown();
            try { if (!releaseWrite.await(2, TimeUnit.SECONDS)) throw new IOException("test write timed out"); }
            catch (InterruptedException e) { Thread.currentThread().interrupt(); throw new IOException(e); }
        }
        if (failRuntime) throw new SecurityException("injected permission failure");
        if (fail) throw new IOException("injected failure");
        this.enabled=enabled; this.saved=saved;
        if (failDurability) { failDurability=false; throw new DurabilityException(); }
        if (failAfterWrite) { failAfterWrite=false; throw new IOException("injected post-commit failure"); }
    }
}
final class Refl { static void log(String ignored) {} }
final class ConstructorProbe {
    boolean enabled=true, saved=true, connected=true, preparing;
    final WifiPersistentState stateStore;
    ConstructorProbe(WifiPersistentState stateStore, boolean preparing) throws Exception {
        CONSTRUCTOR_INIT
    }
}
final class Test {
    interface Listener { boolean onConnectionChanged(boolean connected, Object info); }
    interface StartupConfirmation { boolean confirmed(boolean expectedConnected); }
    final Object transitionLock=new Object();
    boolean enabled=true, saved=true, down=false, connected=true, networkUnknown=false, preparing=false, serviceLooper=false, activationInProgress=false, activationInterrupted=false;
    final WifiPersistentState stateStore=new WifiPersistentState();
    boolean networkConnected=true, failRollback=false;
    int broadcasts, configuredBroadcasts;
    Listener listener = (value, info) -> {
        if (value && failRollback) { networkUnknown=true; return false; }
        networkConnected=value; return true;
    };
    Object wifiInfo(boolean ignored) { return new Object(); }
    synchronized boolean isConnected() { return connected; }
    boolean onServiceLooper() { return serviceLooper; }
    void queueStateBroadcast(boolean a, boolean b, boolean c, boolean d) { broadcasts++; }
    void queueConfiguredBroadcast(boolean a, boolean b) { if (a != b) configuredBroadcasts++; }
    TRANSITION
    AIRPLANE_DISABLE
    INFO_QUERY
    PREPARE_FINISH
    static void check(boolean ok, String message) { if (!ok) throw new AssertionError(message); }
    public static void main(String[] args) throws Exception {
        Test airplane = new Test();
        check(airplane.disableForAirplaneMode(), "airplane transition failed");
        check(!airplane.enabled && !airplane.connected && !airplane.networkConnected && !airplane.stateStore.enabled,
                "airplane mode did not disconnect and persist Wi-Fi state");
        check(airplane.saved && airplane.stateStore.saved, "airplane mode forgot the saved network");
        Test t=new Test();
        check(t.transition(false, true, false), "disable failed");
        check(!t.enabled && !t.connected && !t.networkConnected && !t.stateStore.enabled, "disable not durable");
        check(t.transition(true, true, false), "enable failed");
        check(t.enabled && t.connected && t.networkConnected && t.stateStore.enabled, "enable not durable");
        check(t.transition(true, false, true), "forget failed");
        check(!t.saved && !t.connected && !t.networkConnected && !t.stateStore.saved, "forget not durable");
        check(t.configuredBroadcasts==1, "forget must notify configuration listeners");
        check(t.transition(true, true, false), "reconnect failed");
        check(t.saved && t.connected && t.networkConnected && t.stateStore.saved, "reconnect not durable");
        check(t.configuredBroadcasts==2, "save must notify configuration listeners");
        t.stateStore.fail=true;
        int oldBroadcasts=t.broadcasts;
        check(!t.transition(false, true, false), "write failure reported success");
        check(t.enabled && t.connected && t.networkConnected && t.stateStore.enabled, "failed write not rolled back");
        check(t.broadcasts==oldBroadcasts, "failed transition broadcast success");
        t.stateStore.fail=false; t.stateStore.failRuntime=true;
        check(!t.transition(false, true, false), "runtime write failure reported success");
        check(t.enabled && t.connected && t.networkConnected, "runtime write failure not rolled back");
        t.stateStore.failRuntime=false; t.stateStore.failAfterWrite=true;
        check(!t.transition(false, true, false), "post-commit failure reported success");
        check(t.enabled && t.connected && t.networkConnected && t.stateStore.enabled, "post-commit storage not restored");
        t.stateStore.failDurability=true;
        check(!t.transition(false, true, false), "directory sync failure reported success");
        check(t.networkUnknown, "directory sync failure must mark durability uncertain");
        t.networkUnknown=false;
        t.stateStore.fail=true; t.stateStore.failRead=true;
        check(!t.transition(false, true, false), "unreadable rollback reported success");
        check(t.networkUnknown, "unverifiable disk rollback must mark state unknown");
        t.networkUnknown=false; t.stateStore.failRead=false;
        t.failRollback=true;
        check(!t.transition(false, true, false), "failed rollback reported success");
        check(t.networkUnknown, "uncertain rollback must not be presented as known");
        Test concurrent=new Test();
        concurrent.stateStore.block=true;
        Thread transition = new Thread(() -> concurrent.transition(false, true, false));
        transition.start();
        check(concurrent.stateStore.writing.await(2, TimeUnit.SECONDS), "transition did not reach storage");
        CountDownLatch readFinished = new CountDownLatch(1);
        Thread reader = new Thread(() -> { concurrent.wifiInfo(); readFinished.countDown(); });
        reader.start();
        check(!readFinished.await(150, TimeUnit.MILLISECONDS), "query observed old state during network transition");
        concurrent.stateStore.releaseWrite.countDown();
        check(readFinished.await(2, TimeUnit.SECONDS), "query did not resume after transition");
        transition.join(2000); reader.join(2000);
        Test queuedCallback = new Test();
        queuedCallback.preparing=true; queuedCallback.connected=false; queuedCallback.networkConnected=false;
        CountDownLatch callbackCompleted=new CountDownLatch(1);
        boolean activated=queuedCallback.finishPreparation(expected -> {
            if(!expected)return !queuedCallback.networkConnected;
            new Thread(() -> {synchronized(queuedCallback.transitionLock){callbackCompleted.countDown();}}).start();
            try{return callbackCompleted.await(300,TimeUnit.MILLISECONDS);}catch(InterruptedException e){return false;}
        });
        check(activated, "registration confirmation blocked queued transition-lock callback");
        Test early = new Test();
        early.preparing=true; early.connected=false; early.networkConnected=false;
        check(!early.transition(true, true, false), "early client connected before network readiness");
        check(!early.connected && !early.networkConnected, "preparation falsely reported connected");
        check(early.finishPreparation(expected -> early.networkConnected == expected), "ready network failed to activate");
        check(!early.preparing && early.connected && early.networkConnected, "ready connection was not committed");
        Test interrupted = new Test();
        interrupted.preparing=true;interrupted.connected=false;interrupted.networkConnected=false;
        check(!interrupted.finishPreparation(expected -> {
            if(expected){synchronized(interrupted.transitionLock){interrupted.activationInterrupted=true;interrupted.down=true;interrupted.networkConnected=false;}return true;}
            return !interrupted.networkConnected;
        }), "unwanted callback during registration reported connected");
        check(interrupted.preparing && !interrupted.connected && !interrupted.networkConnected && !interrupted.networkUnknown,
                "interrupted registration did not restore safe preparation");
        Test disabled = new Test();
        disabled.preparing=true; disabled.connected=false; disabled.networkConnected=false;
        check(disabled.transition(false, true, false), "early disable failed");
        check(disabled.finishPreparation(expected -> disabled.networkConnected == expected), "disabled service failed to activate");
        check(!disabled.preparing && !disabled.enabled && !disabled.connected && !disabled.networkConnected,
                "disabled preparation unexpectedly connected");
        Test forgotten = new Test();
        forgotten.preparing=true; forgotten.connected=false; forgotten.networkConnected=false;
        check(forgotten.transition(true, false, true), "forget during preparation failed");
        check(forgotten.finishPreparation(expected -> forgotten.networkConnected == expected), "forgotten service failed to activate");
        check(!forgotten.preparing && !forgotten.saved && !forgotten.connected && !forgotten.networkConnected,
                "forgotten preparation unexpectedly connected");
        Test failed = new Test();
        failed.preparing=true; failed.connected=false; failed.networkConnected=false; failed.listener=null;
        check(!failed.finishPreparation(expected -> failed.networkConnected == expected), "missing network listener reported success");
        check(failed.preparing && !failed.connected, "failed activation did not stay disconnected");
        failed.listener = (value, info) -> false;
        check(!failed.finishPreparation(expected -> failed.networkConnected == expected), "failed network activation reported success");
        check(failed.preparing && !failed.connected, "failed network activation did not restore preparation");
        Test unknown = new Test();
        unknown.preparing=true; unknown.connected=false; unknown.networkConnected=false; unknown.networkUnknown=true;
        check(!unknown.finishPreparation(expected -> unknown.networkConnected == expected) && unknown.preparing && !unknown.connected,
                "unknown network outcome must not activate");
        unknown.networkUnknown=false;
        unknown.listener = (value, info) -> { unknown.networkConnected=true; unknown.networkUnknown=true; return true; };
        check(!unknown.finishPreparation(expected -> unknown.networkConnected == expected) && unknown.preparing && unknown.networkUnknown && !unknown.connected,
                "network failure during activation must remain unknown and disconnected");
        Test exceptional = new Test();
        exceptional.preparing=true; exceptional.connected=false; exceptional.networkConnected=false;
        exceptional.listener = (value, info) -> { throw new IllegalStateException("injected listener exception"); };
        try { exceptional.finishPreparation(expected -> exceptional.networkConnected == expected); throw new AssertionError("listener exception was swallowed"); }
        catch (IllegalStateException expected) { }
        check(exceptional.preparing && exceptional.networkUnknown && !exceptional.connected,
                "listener exception must leave network outcome unknown");
        WifiPersistentState stored = new WifiPersistentState();
        check(!new ConstructorProbe(stored, true).connected, "preparing constructor reported connected");
        check(new ConstructorProbe(stored, false).connected, "default constructor no longer starts connected");
        stored.enabled=false;
        ConstructorProbe switchedOff = new ConstructorProbe(stored, true);
        check(!switchedOff.enabled && !switchedOff.connected, "preparing constructor ignored stored switch");
        stored.failRead=true;
        try { new ConstructorProbe(stored, true); throw new AssertionError("state read failure ignored"); }
        catch (IOException expected) { }
        Test concurrentPrepare = new Test();
        concurrentPrepare.preparing=true; concurrentPrepare.connected=false; concurrentPrepare.networkConnected=false;
        CountDownLatch starting = new CountDownLatch(1), releaseStart = new CountDownLatch(1);
        concurrentPrepare.listener = (value, info) -> {
            if (value) {
                starting.countDown();
                try { if (!releaseStart.await(2, TimeUnit.SECONDS)) return false; }
                catch (InterruptedException e) { Thread.currentThread().interrupt(); return false; }
            }
            concurrentPrepare.networkConnected=value;
            return true;
        };
        Thread activation = new Thread(() -> concurrentPrepare.finishPreparation(expected -> concurrentPrepare.networkConnected == expected));
        activation.start();
        check(starting.await(2, TimeUnit.SECONDS), "activation did not start");
        CountDownLatch disabledDone = new CountDownLatch(1);
        boolean[] concurrentToggle = new boolean[1];
        Thread disable = new Thread(() -> { concurrentToggle[0]=concurrentPrepare.transition(false, true, false); disabledDone.countDown(); });
        disable.start();
        check(disabledDone.await(1, TimeUnit.SECONDS) && !concurrentToggle[0], "toggle during activation was not rejected");
        releaseStart.countDown();
        activation.join(2000); disable.join(2000);
        check(concurrentPrepare.transition(false,true,false), "toggle retry after activation failed");
        check(!concurrentPrepare.connected && !concurrentPrepare.networkConnected && !concurrentPrepare.enabled,
                "toggle retry after activation not applied");
        Test unconfirmed = new Test();
        unconfirmed.preparing=true; unconfirmed.connected=false; unconfirmed.networkConnected=false;
        check(!unconfirmed.finishPreparation(expected -> !expected && !unconfirmed.networkConnected),
                "unconfirmed registered network reported connected");
        check(unconfirmed.preparing && !unconfirmed.connected && !unconfirmed.networkConnected && !unconfirmed.networkUnknown,
                "unconfirmed registration was not safely removed");
        Test sameLooper = new Test();
        sameLooper.preparing=true; sameLooper.connected=false; sameLooper.networkConnected=false; sameLooper.serviceLooper=true;
        try { sameLooper.finishPreparation(expected -> true); throw new AssertionError("activation on service looper accepted"); }
        catch (IllegalStateException expected) { }
        check(sameLooper.preparing && !sameLooper.networkConnected, "service looper attempted activation");
        Test unknownCleanup = new Test();
        unknownCleanup.preparing=true; unknownCleanup.connected=false; unknownCleanup.networkConnected=false;
        check(!unknownCleanup.finishPreparation(expected -> false), "unconfirmed cleanup reported success");
        check(unknownCleanup.preparing && !unknownCleanup.connected && unknownCleanup.networkUnknown,
                "unconfirmed cleanup did not mark state unknown");
        Test residual = new Test();
        residual.preparing=true; residual.enabled=false; residual.connected=false; residual.networkConnected=true;
        check(!residual.finishPreparation(expected -> residual.networkConnected == expected),
                "disabled startup accepted an unexpected network");
        check(residual.preparing && residual.networkUnknown, "unexpected residual network not marked unknown");
        System.out.println("Wi-Fi durable transition tests passed");
    }
}
'''.replace('TRANSITION', method).replace('AIRPLANE_DISABLE', airplane_method).replace('INFO_QUERY', info_method).replace('PREPARE_FINISH', prepare_method).replace('CONSTRUCTOR_INIT', constructor_init)

with tempfile.TemporaryDirectory(prefix='rme-wifi-state-transition-') as tmp:
    java = Path(tmp) / 'Test.java'
    java.write_text(fixture, encoding='utf-8')
    subprocess.run(['javac', '--release', '8', '-Xlint:-options', '-d', tmp, str(java)], check=True)
    subprocess.run(['java', '-cp', tmp, 'app.aemu.stub.Test'], check=True)
