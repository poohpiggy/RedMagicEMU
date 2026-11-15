"""Exercise the control-service wait against a deterministic fake clock and Binder."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FAKES = {
    "android/os/IBinder.java": "package android.os; public interface IBinder { boolean isBinderAlive(); }",
    "android/os/SystemClock.java": "package android.os; public class SystemClock { public static long elapsedRealtime() { return 0; } }",
    "app/aemu/stub/ControlWaitTest.java": r"""package app.aemu.stub;
        import android.os.IBinder; import java.lang.reflect.*;
        public final class ControlWaitTest {
          static Class<?> waiter, finderType, clockType; static Method await;
          static class Binder implements IBinder { final boolean alive; Binder(boolean b) { alive=b; }
            public boolean isBinderAlive() { return alive; } }
          static class Env implements InvocationHandler {
            long now, findDelay, readinessDelay; int finds, sleeps, readinessChecks; boolean interruptSleep, interruptFind, interruptReadiness, lookupFailure, readinessFailure, alwaysDead, alwaysLive, deferReady;
            final Binder live=new Binder(true), dead=new Binder(false);
            public Object invoke(Object p, Method m, Object[] args) throws Throwable {
              if (m.getName().equals("find")) {
                finds++; if (lookupFailure) throw new IllegalStateException("lookup failed");
                if (interruptFind) throw new InterruptedException("lookup interrupted");
                now += findDelay;
                if (alwaysDead) return dead;
                if (alwaysLive) return live;
                return now >= 200 ? live : finds == 1 ? dead : null;
              }
              if (m.getName().equals("isReady")) {
                readinessChecks++; if (args[0] != live) throw new AssertionError("checked wrong binder");
                if (readinessFailure) throw new SecurityException("status denied");
                if (interruptReadiness) throw new InterruptedException("status interrupted");
                now += readinessDelay;
                return !deferReady || now >= 200;
              }
              if (m.getName().equals("elapsedRealtime")) return now;
              if (m.getName().equals("sleep")) {
                sleeps++; if (interruptSleep) throw new InterruptedException("interrupted");
                long delay=(Long) args[0]; if (delay<=0 || delay>100) throw new AssertionError("invalid delay");
                now+=delay; return null;
              }
              throw new AssertionError("unexpected method: "+m.getName());
            }
            Object proxy(Class<?> type) { return Proxy.newProxyInstance(type.getClassLoader(), new Class<?>[]{type},this); }
            IBinder run(long timeout) throws Exception {
              try { return (IBinder) await.invoke(null,proxy(finderType),proxy(clockType),timeout); }
              catch (InvocationTargetException e) {
                Throwable cause=e.getCause(); if (cause instanceof Exception) throw (Exception) cause;
                if (cause instanceof Error) throw (Error) cause; throw e;
              }
            }
          }
          static void check(boolean v,String m) { if (!v) throw new AssertionError(m); }
          public static void main(String[] args) throws Exception {
            try {
              waiter=Class.forName("app.aemu.stub.WifiControlWaiter");
              finderType=Class.forName("app.aemu.stub.WifiControlWaiter$Finder");
              clockType=Class.forName("app.aemu.stub.WifiControlWaiter$Clock");
              await=waiter.getDeclaredMethod("await",finderType,clockType,long.class); await.setAccessible(true);
            } catch (ReflectiveOperationException missing) {
              throw new AssertionError("bounded control-service waiter missing", missing);
            }
            Env delayed=new Env();
            check(delayed.run(500)==delayed.live, "delayed live binder not returned");
            check(delayed.finds==3 && delayed.sleeps==2 && delayed.now==200 && delayed.readinessChecks==1,
                    "dead/null services not retried safely");

            Env unarmed=new Env(); unarmed.alwaysLive=true; unarmed.deferReady=true;
            check(unarmed.run(500)==unarmed.live && unarmed.now==200 && unarmed.readinessChecks==3,
                    "unarmed control accepted before service readiness");

            Env lateReady=new Env(); lateReady.alwaysLive=true; lateReady.readinessDelay=251; boolean tooLate=false;
            try { lateReady.run(250); } catch (IllegalStateException expected) { tooLate=true; }
            check(tooLate, "readiness accepted after deadline");

            Env timeout=new Env(); timeout.alwaysDead=true; boolean failed=false;
            try { timeout.run(250); } catch (IllegalStateException expected) { failed=true; }
            check(failed && timeout.now==250 && timeout.sleeps==3, "timeout failed or waited too long");

            Env error=new Env(); error.lookupFailure=true; failed=false;
            try { error.run(500); } catch (IllegalStateException expected) { failed="lookup failed".equals(expected.getMessage()); }
            check(failed && error.sleeps==0, "lookup exception swallowed");

            Env statusError=new Env(); statusError.alwaysLive=true; statusError.readinessFailure=true; failed=false;
            try { statusError.run(500); } catch (SecurityException expected) { failed="status denied".equals(expected.getMessage()); }
            check(failed && statusError.sleeps==0, "status exception swallowed");

            Env statusInterrupted=new Env(); statusInterrupted.alwaysLive=true; statusInterrupted.interruptReadiness=true; failed=false;
            try { statusInterrupted.run(500); } catch (InterruptedException expected) { failed=true; }
            check(failed && Thread.currentThread().isInterrupted(), "status interrupt not preserved");
            Thread.interrupted();

            Env late=new Env(); late.now=200; late.findDelay=251; failed=false;
            try { late.run(250); } catch (IllegalStateException expected) { failed=true; }
            check(failed && late.sleeps==0, "late lookup accepted after deadline");

            Env interruptedFind=new Env(); interruptedFind.interruptFind=true; failed=false;
            try { interruptedFind.run(500); } catch (InterruptedException expected) { failed=true; }
            check(failed && Thread.currentThread().isInterrupted(), "lookup interrupt not preserved");
            Thread.interrupted();

            Env interrupted=new Env(); interrupted.interruptSleep=true; failed=false;
            try { interrupted.run(500); } catch (InterruptedException expected) { failed=true; }
            check(failed && Thread.currentThread().isInterrupted(), "interrupt not propagated and preserved");
            Thread.interrupted();

            Env preInterrupted=new Env(); Thread.currentThread().interrupt(); failed=false;
            try { preInterrupted.run(500); } catch (InterruptedException expected) { failed=true; }
            check(failed && Thread.currentThread().isInterrupted() && preInterrupted.finds==0,
                    "preexisting interrupt ignored");
            Thread.interrupted();
            System.out.println("WifiControlWaitTest passed");
          }
        }""",
}


def main():
    with tempfile.TemporaryDirectory(prefix="rme-control-wait-") as temporary:
        work = Path(temporary)
        sources = []
        for name, source in FAKES.items():
            path = work / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(source, encoding="utf-8")
            sources.append(path)
        helper = ROOT / "src/app/aemu/stub/WifiControlWaiter.java"
        if helper.exists():
            sources.append(helper)
        subprocess.run(["javac", "--release", "8", "-Xlint:-options", "-d", str(work), *map(str, sources)], check=True)
        subprocess.run(["java", "-cp", str(work), "app.aemu.stub.ControlWaitTest"], check=True)


if __name__ == "__main__":
    main()
