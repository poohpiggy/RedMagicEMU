"""Verify the airplane observer is started with the target Guest service."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = root / "src/app/aemu/stub"
startup = (source / "EarlyWifiPublishExperiment.java").read_text(encoding="utf-8")
service = (source / "WifiService.java").read_text(encoding="utf-8")
reader = (source / "WifiSettingsReader.java").read_text(encoding="utf-8")
assert "service.startAirplaneModeObserver();" in startup
assert "WifiSettingsReader.airplaneMode(context)" in service
assert "restoreAfterAirplaneMode() : disableForAirplaneMode()" in service
assert 'globalInt(context, "airplane_mode_on")' in reader
print("WIFI_AIRPLANE_WIRING_OK")
