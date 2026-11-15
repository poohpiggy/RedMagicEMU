from pathlib import Path
source=(Path(__file__).resolve().parents[5]/'main/java/app/aemu/core/GuestVm.kt').read_text(encoding='utf8')
start=source.index('    fun stop() {');end=source.index('    /** The stop-time sync',start);stop=source[start:end]
assert stop.index('captureDnsStopFailure { closeWifiDns() }') < stop.index('closeRadioChannelForStop') < stop.index('adb.requestStop()') < stop.index('killAll()') < stop.index('dnsCleanupFailure?.let { failure ->')
assert 'wifiDnsRetirement.requireComplete()' in source
assert 'if (!wifiDnsRetirement.canStart()) null' in source
print('PASS GuestVm DNS stop cleanup and restart guard wiring')
