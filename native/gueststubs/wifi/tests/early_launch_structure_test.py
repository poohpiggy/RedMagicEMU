from pathlib import Path
s=(Path(__file__).resolve().parent.parent/'src/app/aemu/stub/EarlyWifiPublishExperiment.java').read_text()
main=s[s.index('public static void main('):]
assert 'new WifiService(cfg,Looper.myLooper(),context,state,true)' in main
assert main.index('new Thread(()->{') < main.index('EarlyWifiPublication.publish(') < main.index('post(startup::start)') < main.index('Looper.loop();')
assert 'EarlyWifiHandoff.run(context,serviceLooper,cfg,service,session)' in main and main.index('EarlyWifiPublication.publish(') < main.index('EarlyWifiHandoff.run(')
print('PASS early startup worker, prepared service and Ethernet retained structure')
