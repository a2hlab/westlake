import sys,time,base64,subprocess,json
import device
r=device.ROOT
script=(r/'read_webview_dom.ps1').read_text();encoded=base64.b64encode(script.encode('utf-16le')).decode()
def dom(name):
 p=subprocess.run(['/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe','-NoProfile','-EncodedCommand',encoded],capture_output=True,timeout=90)
 (r/(name+'.json')).write_bytes(p.stdout);(r/(name+'-errors.log')).write_bytes(p.stderr);assert p.returncode==0
 data=json.loads(p.stdout.decode('utf-8-sig'))
 found=[t['dom'] for t in data if t.get('dom') and t['dom'].get('title')=='你好-头条搜索']
 assert len(found)==1,[(x.get('dom') or {}).get('title') for x in data]
 return found[0]
device.shell('power-shell wakeup; aa start -b org.westlake.imehost -a EntryAbility')
for attempt in range(6):
 before=dom('resume27-search-dom-check'+str(attempt))
 print('SEARCH',before['ready'],before['textLength'],before['scrollY'],flush=True)
 if before['ready']=='complete' and before['textLength']>1000:
  (r/'resume27-search-dom-before.json').write_bytes((r/('resume27-search-dom-check'+str(attempt)+'.json')).read_bytes());break
 time.sleep(2)
else:raise AssertionError('Search result did not reach complete DOM gate')
device.save('resume27-search-focused-windows.log',"hidumper -s WindowManagerService -a '-a'")
device.shell('uinput -T -m 600 1500 600 650 400');time.sleep(.5)
after=dom('resume27-search-dom-after');print('SCROLL',before['scrollY'],after['scrollY'],flush=True)
assert after['scrollY']>before['scrollY']
