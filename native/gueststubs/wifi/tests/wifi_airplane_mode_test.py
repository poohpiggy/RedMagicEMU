"""Exercise Guest airplane-mode transitions without a phone or host Wi-Fi."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = root / "src/app/aemu/stub/WifiAirplaneModeObserver.java"
test = root / "tests/WifiAirplaneModeObserverTest.java"
with tempfile.TemporaryDirectory(prefix="rme-wifi-airplane-") as tmp:
    # The old service has no observer, faithfully modeling its no-op behavior.
    if not source.exists():
        source = Path(tmp) / "WifiAirplaneModeObserver.java"
        source.write_text("package app.aemu.stub; final class WifiAirplaneModeObserver { "
                          "interface Disable { boolean disable(); } "
                          "WifiAirplaneModeObserver(Disable d) {} void observe(boolean on) {} }", encoding="utf8")
    subprocess.run(["javac", "-d", tmp, str(source), str(test)], check=True)
    subprocess.run(["java", "-cp", tmp, "app.aemu.stub.WifiAirplaneModeObserverTest"], check=True)
