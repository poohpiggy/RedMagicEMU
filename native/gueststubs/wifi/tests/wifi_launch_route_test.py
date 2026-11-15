from pathlib import Path
root=Path(__file__).resolve().parents[4]
launch=(root/'app/src/main/java/app/aemu/core/TargetWifiLaunch.kt').read_text(encoding='utf8')
vm=(root/'app/src/main/java/app/aemu/core/GuestVm.kt').read_text(encoding='utf8')
stub=(root/'native/gueststubs/wifi/src/app/aemu/stub/WifiStub.java').read_text(encoding='utf8')
artifacts=(root/'app/src/main/java/app/aemu/core/TargetWifiArtifacts.kt').read_text(encoding='utf8')
start=stub.index('public static void main(String[] args)')
end=stub.index('private static IBinder checkService',start)
main=stub[start:end]
assert 'app.aemu.stub.WifiStub' in launch, 'host no longer uses gated WifiStub entry'
assert 'if (targetWifi)' in vm and 'startService(TargetWifiLaunch.wifiService())' in vm, 'target-only launch changed'
assert 'const val RUNTIME_READY = true' in artifacts, 'target Wi-Fi must be enabled'
assert 'SERVICE_IMPLEMENTATION_READY = true' in stub, 'guest Wi-Fi service must be enabled'
early=(root/'native/gueststubs/wifi/src/app/aemu/stub/EarlyWifiPublishExperiment.java').read_text(encoding='utf8')
early_main=early[early.index('public static void main(String[] args)'):]
assert 'WifiStub.requireServiceReady();' in early_main, 'direct early entry bypasses guest gate'
assert early_main.index('WifiStub.requireServiceReady();') < early_main.index('Looper.prepare();'), 'direct entry performs setup before its gate'
assert 'EarlyWifiPublishExperiment.main(args);' in main, 'guest entry does not delegate to early publication'
assert main.index('if (!SERVICE_IMPLEMENTATION_READY)') < main.index('EarlyWifiPublishExperiment.main(args);'), 'early startup bypasses guest gate'
print('PASS target launch reaches gated early Wi-Fi entry')
