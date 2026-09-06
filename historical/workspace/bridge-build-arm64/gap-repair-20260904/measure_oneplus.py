"""Identical local workload on OnePlus; not a live Toutiao/presentation benchmark."""
import argparse,hashlib,json,re,subprocess,time
from pathlib import Path
ROOT=Path(__file__).resolve().parent
ADB=['/mnt/c/Users/dspfa/Dev/platform-tools/adb.exe','-s','cfb7c9e3']
p=argparse.ArgumentParser();p.add_argument('run');p.add_argument('--apk',default='control-out/control.apk');a=p.parse_args()
assert re.fullmatch('oneplus[0-9a-z-]+',a.run)
assert not (ROOT/(a.run+'.json')).exists()
def adb(*args):
    result=subprocess.run(ADB+list(args),cwd=ROOT,capture_output=True,timeout=45)
    text=result.stdout.decode(errors='replace').replace('\r','')
    with (ROOT/(a.run+'-commands.jsonl')).open('a') as log:
        log.write(json.dumps(dict(args=args,rc=result.returncode,output=text,error=result.stderr.decode(errors='replace')))+'\n')
    assert result.returncode==0,text
    return text
apk=(ROOT/a.apk).resolve();assert apk.is_relative_to(ROOT) and apk.suffix=='.apk';sha=hashlib.sha256(apk.read_bytes()).hexdigest()
assert 'Success' in adb('install','-r',str(apk.relative_to(ROOT)))
network=adb('shell','dumpsys connectivity')
(ROOT/(a.run+'-network.log')).write_text(network)
# Android can report no physical default network while an IPv4 USB VPN is
# connected and validated. Record that transport instead of declaring it offline.
validated_vpn=any('NetworkAgentInfo{' in line and 'Transports: VPN' in line
                  and 'VALIDATED' in line and 'VPN CONNECTED' in line
                  for line in network.splitlines())
physical_default=bool(re.search(r'^Active default network: (?!none\b)\S+',network,re.M))
adb('shell','am force-stop org.westlake.gapcontrol')
adb('shell','am start -W -n org.westlake.gapcontrol/.BenchmarkActivity')
pid=adb('shell','pidof org.westlake.gapcontrol').strip();assert pid.isdigit()
for _ in range(35):
    log=adb('logcat','-d','--pid='+pid,'-v','brief')
    if '[GAP_BENCH] PAGE_COMPLETE samples=12 warmup=2' in log:break
    if '[GAP_BENCH] FAIL' in log:break
    time.sleep(.5)
(ROOT/(a.run+'-pages.log')).write_text(log)
assert '[GAP_BENCH] PAGE_COMPLETE samples=12 warmup=2' in log
for _ in range(12):adb('shell','input tap 550 240');time.sleep(.18)
time.sleep(.3);log=adb('logcat','-d','--pid='+pid,'-v','brief')
(ROOT/(a.run+'.log')).write_text(log)
assert len(re.findall(r'INPUT sample=',log))==12
assert len(re.findall(r'INPUT_FRAME sample=',log))==12
remote=adb('shell','pm path org.westlake.gapcontrol').strip().removeprefix('package:')
assert adb('shell','sha256sum '+remote).split()[0]==sha
(ROOT/(a.run+'.json')).write_text(json.dumps(dict(pid=int(pid),control_sha256=sha,
    live_toutiao=False,physical_presentation=False,network_active=physical_default or validated_vpn,
    validated_vpn=validated_vpn,physical_default_network=physical_default,
    network_transport='validated-vpn' if validated_vpn else ('physical-default' if physical_default else 'offline')),
    indent=2)+'\n')
print('ONEPLUS_OFFLINE_COMPLETE',a.run,sha)
