import argparse,json,re
import device
p=argparse.ArgumentParser();p.add_argument('run');a=p.parse_args();assert re.fullmatch('[a-z0-9-]+',a.run)
state=json.loads((device.ROOT/(a.run+'-processes.json')).read_text())
log=device.save(a.run+'-child-current.log',f'cat /data/service/el1/public/appspawnx/adapter_child_{state["child"]}.stderr')
for line in log.splitlines():
    if any(tag in line for tag in ('GAP_NET','GAP_IME','GAP_TRUST','GAP_CERT','GAP_CRYPTO','GAP_BENCH','OH_INSETS','OH_NETWORK','OH_WEBVIEW_ORDER','[INITCHILD-FAIL]   caused by')):print(line)
