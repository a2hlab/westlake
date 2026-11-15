"""Check ledger consistency and exercise the offline report in Chromium via CDP."""
import asyncio
import base64
import csv
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import urllib.request
import websockets

BASE=Path('/home/dspfac/a2hlab')
OUT=BASE/'reports/hk-top1000-audit-20261115'


async def browser_check():
    chrome=Path.home()/'.cache/ms-playwright/chromium-1254/chrome-linux64/chrome'
    profile=Path(tempfile.mkdtemp(prefix='audit-browser-',dir=BASE/'research_notes/hk-top1000-audit-20261115'))
    log=profile/'browser.log'
    proc=subprocess.Popen([str(chrome),'--headless','--no-sandbox','--disable-gpu','--disable-background-networking',
                           '--no-first-run','--remote-debugging-port=0','--user-data-dir='+str(profile),'about:blank'],
                           stdout=log.open('w'),stderr=subprocess.STDOUT)
    try:
        for _ in range(100):
            if (profile/'DevToolsActivePort').exists():break
            await asyncio.sleep(.1)
        port=(profile/'DevToolsActivePort').read_text().splitlines()[0]
        pages=json.load(urllib.request.urlopen('http://127.0.0.1:'+port+'/json/list'))
        async with websockets.connect(pages[0]['webSocketDebuggerUrl'],max_size=20*1024*1024) as ws:
            seq=0;errors=[];network=[]
            async def cmd(method,params=None):
                nonlocal seq
                seq+=1;expected=seq
                await ws.send(json.dumps({'id':seq,'method':method,'params':params or {}}))
                while True:
                    r=json.loads(await asyncio.wait_for(ws.recv(),20))
                    if r.get('method')=='Runtime.exceptionThrown':errors.append(r['params'])
                    if r.get('method')=='Network.requestWillBeSent':network.append(r['params']['request']['url'])
                    if r.get('id')==expected:
                        assert 'error' not in r,r
                        return r.get('result',{})
            async def js(expression):
                r=await cmd('Runtime.evaluate',{'expression':expression,'returnByValue':True})
                assert 'exceptionDetails' not in r,r
                return r['result'].get('value')
            await cmd('Runtime.enable');await cmd('Page.enable');await cmd('Network.enable')
            await cmd('Emulation.setDeviceMetricsOverride',{'width':1440,'height':1100,'deviceScaleFactor':1,'mobile':False})
            await cmd('Page.navigate',{'url':(OUT/'index.html').as_uri()})
            for _ in range(100):
                if await js("document.readyState==='complete' && document.getElementById('result-count')?.textContent?.includes('1,000 of 1,000')"):
                    break
                await asyncio.sleep(.1)
            assert await js("document.querySelectorAll('#app-table tbody tr').length")==50
            assert await js("document.getElementById('result-count').textContent")=='1,000 of 1,000 apps match'
            shot=await cmd('Page.captureScreenshot',{'format':'png'})
            (OUT/'report-desktop.png').write_bytes(base64.b64decode(shot['data']))
            before=await js("document.getElementById('google').outerHTML")
            changed=await js("document.getElementById('google').value='refused'; ({value:document.getElementById('google').value,n:filtered.length})")
            assert changed['value']=='refused',(changed,before)
            changed=await js("applyFilters(); ({value:document.getElementById('google').value,n:filtered.length})")
            assert changed['value']=='refused',changed
            got=await js("({n:filtered.length,g:document.getElementById('google').value,s:document.getElementById('stage').value,t:document.getElementById('testing').value,q:document.getElementById('search').value})")
            assert got['n']==26,got
            await js("document.getElementById('google').value='';document.getElementById('stage').value='unscored';applyFilters()")
            assert await js("filtered.length")==22
            await js("document.getElementById('stage').value='';document.getElementById('testing').value='no-exploration';applyFilters()")
            assert await js("filtered.length")==485
            await js("document.getElementById('testing').value='';document.getElementById('search').value='com.ubercab';applyFilters()")
            assert await js("filtered.length")==1
            await js("document.querySelector('.app-link').click()")
            assert await js("document.getElementById('app-detail').open")
            assert 'Uber' in await js("document.getElementById('detail-title').textContent")
            assert 'unverified' in await js("document.getElementById('detail-content').textContent")
            await js("document.getElementById('close-detail').click();document.getElementById('search').value='';applyFilters();document.getElementById('next').click()")
            assert await js("page")==1
            await js("document.getElementById('prev').click()")
            assert await js("page")==0
            await cmd('Emulation.setDeviceMetricsOverride',{'width':390,'height':844,'deviceScaleFactor':1,'mobile':True})
            await js("window.scrollTo(0,0)")
            assert await js('document.documentElement.scrollWidth <= window.innerWidth'), 'Page overflows mobile viewport'
            shot=await cmd('Page.captureScreenshot',{'format':'png'})
            (OUT/'report-mobile.png').write_bytes(base64.b64decode(shot['data']))
            assert not errors,errors
            assert all(url.startswith('file:') for url in network),network
            return {'browser':'Chromium 1254','initial_rows':50,'refusal_filter':26,'unscored_filter':22,
                    'no_exploration_filter':485,'package_search':'pass','detail_dialog':'pass','pagination':'pass',
                    'mobile_page_overflow':False,'javascript_errors':0,'external_requests':0}
    finally:
        proc.terminate()
        try:proc.wait(timeout=10)
        except subprocess.TimeoutExpired:proc.kill();proc.wait()
        for _ in range(20):
            try:shutil.rmtree(profile);break
            except OSError:await asyncio.sleep(.1)


def main():
    apps=json.loads((OUT/'apps.json').read_text());rows=list(csv.DictReader((OUT/'apps.csv').open()))
    h=(OUT/'index.html').read_text();m=(OUT/'all-apps.md').read_text();s=json.loads((OUT/'summary.json').read_text())
    assert len(rows)==1000 and len(re.findall(r'^## \d+\.',m,re.M))==1000
    assert {r['package'] for r in rows}=={r['package'] for r in apps}
    embedded=json.loads(re.search(r'<script id="audit-data" type="application/json">(.*?)</script>',h,re.S)[1])
    assert len(embedded['apps'])==1000 and embedded['summary']==s
    assert sum(s['latest_saved_launch'].values())==1000
    assert sum(s['exploration_latest'].values())==1000
    assert sum(s['blocker_categories'].values())==193
    assert sum(x['post_cutoff_log_count'] for x in apps)==s['post_cutoff_logs']
    assert sum(bool(x['post_cutoff_status9_services']) for x in apps)==26
    for r in apps:
        assert all(r[k] for k in ['running_description','blocked_description','tested_description','gaps_description','next_test'])
    for filename in ['apps.csv','all-apps.md','index.html','apps.json']:
        text=(OUT/filename).read_text().replace('access_token: the host application','OH token component: the host application')
        assert not re.search(r'[\w.+-]+@[\w.-]+\.[A-Za-z]{2,}',text),filename+' email pattern'
        assert not re.search(r'(?i)(?:access_token|refresh_token|gsf_id|password)\s*[:=]\s*[^\s<]',text),filename+' secret pattern'
    browser=asyncio.run(browser_check())
    v=json.loads((OUT/'validation.json').read_text())
    v.update(csv_markdown_html_json_rows=1000,all_descriptions_present=True,privacy_pattern_check='pass',browser=browser)
    (OUT/'validation.json').write_text(json.dumps(v,indent=2)+'\n')
    hashes={}
    for p in sorted(OUT.iterdir()):
        if p.is_file() and p.name!='artifact-hashes.json':hashes[p.name]=hashlib.sha256(p.read_bytes()).hexdigest()
    for p in sorted((BASE/'research_notes/hk-top1000-audit-20261115').iterdir()):
        if p.is_file():hashes['../../research_notes/hk-top1000-audit-20261115/'+p.name]=hashlib.sha256(p.read_bytes()).hexdigest()
    (OUT/'artifact-hashes.json').write_text(json.dumps(hashes,indent=2)+'\n')
    print(json.dumps(v,indent=2))


if __name__=='__main__':main()
