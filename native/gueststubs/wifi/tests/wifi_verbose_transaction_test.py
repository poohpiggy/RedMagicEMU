"""Exercise the actual WifiService transaction dispatch prefix without Android.

The target-ROM Binder test in WifiGuestBinderSmoke covers the real Parcel and
transaction number; this host test keeps the dispatch and reply contract quick
to verify without starting a guest or publishing a service.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
source = (root / "src/app/aemu/stub/WifiService.java").read_text(encoding="utf-8")
signature = "protected boolean onTransact(int code, Parcel data, Parcel reply, int flags)"
start = source.index("{", source.index(signature)) + 1
end = source.index("        if (WifiServiceGuard.isSignalLevel(name)) {", start)
prefix = source[start:end]

fixture = r'''
package app.aemu.stub;
import java.util.HashMap;
class WifiVerboseTransactionTest extends BaseBinder {
    static final int INTERFACE_TRANSACTION = 0x5f4e5446, PROBE = 55, PROBE_ANSWER = 77;
    static final String DESCRIPTOR = "android.net.wifi.IWifiManager";
    final java.util.Map<Integer,String> names = new HashMap<Integer,String>();
    final EarlyWifiProofBinder earlyProof = null;
    static final class EarlyWifiProofBinder {
        static final int PROOF = 0x41454d56;
        boolean answer(Parcel data, Parcel reply) { throw new AssertionError("unexpected proof transaction"); }
    }
    static final class Parcel {
        String token; int remaining; boolean noException; Integer value;
        Parcel(String t, int extra) { token = t; remaining = extra; }
        void enforceInterface(String expected) {
            if (!expected.equals(token)) throw new SecurityException("bad interface");
        }
        void enforceNoDataAvail() {
            if (remaining != 0) throw new IllegalArgumentException("trailing data");
        }
        void writeString(String ignored) {}
        void writeInt(int result) { value = result; }
        void writeNoException() { noException = true; }
    }
    static final class IBinder { static final int FLAG_ONEWAY = 1; }
    protected boolean onTransact(int code, Parcel data, Parcel reply, int flags) {
        PREFIX
        return false;
    }
    static void check(boolean ok, String message) { if (!ok) throw new AssertionError(message); }
    public static void main(String[] args) {
        WifiVerboseTransactionTest service = new WifiVerboseTransactionTest();
        service.names.put(1, "getVerboseLoggingLevel");
        Parcel input = new Parcel(DESCRIPTOR, 0), output = new Parcel(null, 0);
        check(service.onTransact(1, input, output, 0), "verbose query rejected");
        check(output.noException && Integer.valueOf(0).equals(output.value), "wrong verbose result");
        boolean rejected = false;
        try { service.onTransact(1, new Parcel("wrong", 0), new Parcel(null, 0), 0); }
        catch (SecurityException expected) { rejected = true; }
        check(rejected, "wrong interface accepted");
        rejected = false;
        try { service.onTransact(1, new Parcel(DESCRIPTOR, 4), new Parcel(null, 0), 0); }
        catch (IllegalArgumentException expected) { rejected = true; }
        check(rejected, "trailing data accepted");
        check(!service.onTransact(1, new Parcel(DESCRIPTOR, 0), null, 0), "null reply accepted");
        System.out.println("verbose transaction dispatch passed");
    }
}
class BaseBinder {
    protected boolean onTransact(int code, WifiVerboseTransactionTest.Parcel data,
            WifiVerboseTransactionTest.Parcel reply, int flags) { return false; }
}
'''.replace("PREFIX", prefix)

with tempfile.TemporaryDirectory(prefix="rme-wifi-verbose-") as tmp:
    java = Path(tmp) / "WifiVerboseTransactionTest.java"
    java.write_text(fixture, encoding="utf-8")
    guard = root / "src/app/aemu/stub/WifiServiceGuard.java"
    subprocess.run(["javac", "-d", tmp, str(java), str(guard)], check=True)
    subprocess.run(["java", "-cp", tmp, "app.aemu.stub.WifiVerboseTransactionTest"], check=True)
