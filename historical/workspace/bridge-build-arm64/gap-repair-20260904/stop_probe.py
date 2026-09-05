"""Stop only a recorded gap-control run after checking its PID identities."""
import argparse,json,re
import device
p=argparse.ArgumentParser();p.add_argument('run');a=p.parse_args();assert re.fullmatch('[a-z0-9-]+',a.run)
s=json.loads((device.ROOT/(a.run+'-processes.json')).read_text());parent=s['parent'];child=s['child']
assert s['remote']=='/data/local/tmp/asx/gap-repair-20260904/'+a.run
cmd=device.shell(f'cat /proc/{parent}/cmdline')
assert cmd.rstrip('\0\n')==s.get('runtime','/data/local/tmp/asx/ime-ability-window-20260904/candidate04/runtime/appspawn-x'),cmd
environment=device.shell(f"grep -az '^ASX_SOCKET_NAME=' /proc/{parent}/environ")
assert environment.rstrip('\0\n')=='ASX_SOCKET_NAME='+s['socket'].rsplit('/',1)[1],environment
maps=device.shell(f'test -e /proc/{child}/maps && cat /proc/{child}/maps')
assert not maps.strip() or s.get('apk',s['remote']+'/control.apk') in maps
if maps.strip() and s.get('private_data'):
    expected=device.shell('stat -c %i '+s['private_data']).strip()
    assert device.shell(f'stat -c %i /proc/{child}/root/data/data/'+s['package']).strip()==expected
device.save(a.run+'-final-child.log',f'cat /data/service/el1/public/appspawnx/adapter_child_{child}.stderr')
targets=f'{child} {parent}' if maps.strip() else str(parent)
print(device.shell(f'kill -TERM {targets}'))
