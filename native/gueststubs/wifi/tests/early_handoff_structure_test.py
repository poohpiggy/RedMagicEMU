from pathlib import Path
base=Path(__file__).resolve().parent.parent/'src/app/aemu/stub'
boot=(base/'EarlyWifiPublishExperiment.java').read_text()
handoff=(base/'EarlyWifiHandoff.java').read_text()
service=(base/'WifiService.java').read_text()
assert boot.index('service.setEarlyProof(session)') < boot.index('EarlyWifiPublication.publish(') < boot.index('EarlyWifiHandoff.run(context,serviceLooper,cfg,service,session)')
assert 'post(startup::start)' in boot and 'Looper.loop();' in boot
assert handoff.index('sys.boot_completed') < handoff.index('STOP_EARLY,session') < handoff.index('finishPreparation(')
assert 'WifiStub.attachEarlyNetwork(guest)' in handoff and 'waitForNetwork(context,n,true)' in handoff
assert 'EarlyHandoffRecovery.recover(' in handoff and 'restoreWifiBinder' not in handoff
assert 'if (code == EarlyWifiProofBinder.PROOF && earlyProof != null)' in service
print('PASS installed early service and network handoff wiring')
