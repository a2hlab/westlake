"""Build a frozen, per-package evidence ledger for the 1,000 ranked apps.

No device operation. Lifecycle scores are retained historical results; Google events
are parsed from raw logs without treating a successful Binder call as feature success.
"""
from collections import Counter, defaultdict
import csv
import datetime as dt
import hashlib
import io
import json
from pathlib import Path
import re
import subprocess

BASE = Path('/home/dspfac/a2hlab')
C = BASE / 'source-closure/inputs/corpus-hk'
L = BASE / 'source-closure/inputs/logs'
OUT = BASE / 'reports/hk-top1000-audit-20261115'
OUT.mkdir(parents=True, exist_ok=True)
CUTOFF = dt.datetime.fromisoformat('2026-11-14T15:36:46+00:00').timestamp()
ALLOW = {151, 130, 40, 25, 257, 185, 51, 218}
STAGES = ['spawned', 'runtime-init', 'bound', 'activity', 'view', 'drawing']
SCORE = re.compile(r'^hk-(\S+)\s+([0-5])\s+(\S+)\s+(\d+)\s+(\d+)\s+(.*)')
TRACE = re.compile(r'\[WL-PLAY-TRACE\] (out|in) (\S+) code=(\d+)(?: service=(\d+))? (\S*)')
CALLBACK = re.compile(r'IGmsCallbacks code=[13] status=(-?\d+) afterService=(-?\d+)')
BROKER = 'com.google.android.gms.common.internal.IGmsServiceBroker'
provenance = {}


def utc(ts): return dt.datetime.fromtimestamp(ts, dt.timezone.utc).isoformat()


def label(p):
    for root, name in [(C, 'corpus'), (L, 'logs'), (OUT, 'audit')]:
        try: return name + '/' + str(p.relative_to(root))
        except ValueError: pass
    return p.name


def read(p):
    before = p.stat()
    data = p.read_bytes()
    after = p.stat()
    key = label(p)
    provenance[key] = {'source': key, 'sha256': hashlib.sha256(data).hexdigest(), 'bytes': len(data),
                       'mtime_utc': utc(after.st_mtime),
                       'stable_during_read': (before.st_mtime_ns, before.st_size) == (after.st_mtime_ns, after.st_size)}
    return data.decode('utf-8', 'replace')


def safe(value):
    text = str(value)
    text = re.sub(r'[\w.+-]+@[\w.-]+\.[A-Za-z]{2,}', '[email omitted]', text)
    text = re.sub(r'https?://\S+', '[URL omitted]', text)
    text = re.sub(r'(?i)(?:android_id|gsf_id|access_token|refresh_token|password)\s*[:=]\s*\S+', '[credential omitted]', text)
    text = re.sub(r'(?<![\w.])[A-Za-z0-9_+/=-]{50,}(?![\w.])', '[long value omitted]', text)
    return text[:500]


def write_json(name, obj):
    (OUT / name).write_text(json.dumps(obj, ensure_ascii=False, indent=2) + '\n')


def main():
    ranked = list(csv.DictReader(io.StringIO(read(C / 'hk-top1000.csv'))))
    packages = {r['appId'] for r in ranked}
    downloads = json.loads(read(C / 'downloads-hk.json'))
    download_by_package = {v['package']: v for v in downloads.values()}
    source_rows = {r['package']: r for r in csv.DictReader(io.StringIO(read(C / 'sources.csv')))}
    history, rounds = defaultdict(list), {}
    for path in sorted(C.glob('round-*.log')):
        tag = path.stem.removeprefix('round-')
        text = read(path)
        entries, current = {}, None
        for line_no, line in enumerate(text.splitlines(), 1):
            m = SCORE.match(line)
            if m and m[1] in packages:
                package, rung = m[1], int(m[2])
                assert m[3] == STAGES[rung]
                evidence = L / tag / ('hk-' + package + '.stderr')
                ts = evidence.stat().st_mtime if evidence.exists() else path.stat().st_mtime
                description = safe(m[6])
                current = {'round': tag, 'rung': rung, 'stage': STAGES[rung], 'timestamp': ts,
                           'record_utc': utc(ts), 'time_basis': 'stderr-mtime' if evidence.exists() else 'round-log-mtime',
                           'fatal_markers': int(m[5]), 'blocker': '' if description == '-' else description,
                           'blocker_category': description.split(':', 1)[0] if ':' in description else '',
                           'score_source': label(path), 'score_line': line_no,
                           'stderr_source': label(evidence) if evidence.exists() else None, 'annotations': []}
                entries[package] = current
            elif current and line.lstrip().startswith('!!'):
                current['annotations'].append(safe(line.strip()))
        if entries:
            rounds[tag] = entries
            for package, row in entries.items(): history[package].append(row)
    for rows in history.values(): rows.sort(key=lambda r: (r['timestamp'], r['round']))
    print('Lifecycle:', len(history), 'packages,', len(rounds), 'scored rounds', flush=True)

    explore, checks = defaultdict(list), defaultdict(list)
    for path in sorted(L.glob('*/hk-*.explore/explore.json')):
        package = path.parent.name.removeprefix('hk-').removesuffix('.explore')
        if package not in packages: continue
        d = json.loads(read(path))
        if d.get('died_at') is not None: status = 'ended-during-exploration'
        elif d.get('steps') and d.get('empty_dumps', 0) > .6*d['steps']: status = 'mostly-empty-view-trees'
        else: status = 'survived-exploration'
        explore[package].append({'round': path.parent.parent.name, 'status': status, 'record_utc': utc(path.stat().st_mtime),
                                 'source': label(path), **{k: d.get(k) for k in ['seconds', 'steps', 'screens', 'taps', 'scrolls', 'backs', 'empty_dumps', 'died_at', 'died_after']}})
    for path in sorted(L.glob('*/*.checks/checks.json')):
        d = json.loads(read(path)); package = d['app'].removeprefix('hk-')
        if package not in packages: continue
        checks[package].append({'round': path.parent.parent.name, 'record_utc': utc(path.stat().st_mtime),
                                'source': label(path), 'passed': d['passed'], 'of': d['of'],
                                'steps': [{k: safe(v) if isinstance(v, str) else v for k, v in s.items()
                                           if k in ['do', 'ok', 'seconds', 'matched', 'text', 'id']} for s in d.get('steps', [])]})
    for rows in list(explore.values()) + list(checks.values()): rows.sort(key=lambda r:r['record_utc'])

    # Include the newest scored observation for every app, the complete l8f sample,
    # and all retained post-cutoff app stderr logs, including unscored targeted runs.
    selected = {}
    for package, rows in history.items():
        for row in [rows[-1]] + [r for r in rows if r['round'] == 'l8f']:
            p = L / row['round'] / ('hk-' + package + '.stderr')
            if p.exists(): selected[(row['round'], package)] = p
    for p in list(L.glob('*/hk-*.stderr')) + list(L.glob('*-hk-*.stderr')):
        if p.stat().st_mtime < CUTOFF: continue
        if p.parent == L:
            tag, package = p.name.removesuffix('.stderr').split('-hk-', 1)
        else:
            tag, package = p.parent.name, p.name.removeprefix('hk-').removesuffix('.stderr')
        if package not in packages: continue
        key = (tag, package)
        # The nested copy is the harness's retained per-round evidence; use it if present.
        if key not in selected or selected[key].parent == L: selected[key] = p
    service_data = subprocess.check_output(['git', '-C', str(BASE/'scratch-harness-wt'), 'show',
                                           '6a22d743fa487797943199355990d712977b4c95:device/play/gms-service-ids.json'],text=True)
    names = {int(k): v['name'] for k,v in json.loads(service_data)['ids'].items()}
    write_json('gms-service-names.json', names)
    google = defaultdict(list)
    for n, ((tag, package), p) in enumerate(sorted(selected.items()), 1):
        text = read(p)
        services, interfaces, modules, events = {}, {}, set(), []
        explicit_certification = False
        zero_module = False
        negative_module = False
        for line_no, line in enumerate(text.splitlines(), 1):
            if '[WL-PLAY-TRACE]' in line:
                cb = CALLBACK.search(line)
                if cb and int(cb[2]) >= 0:
                    sid, status = int(cb[2]), int(cb[1])
                    s = services.setdefault(sid, {'id': sid, 'name': names.get(sid, 'UNKNOWN'), 'requested': False,
                                                  'broker_replies': set(), 'statuses': set(), 'lines': []})
                    s['statuses'].add(status)
                    if len(s['lines']) < 4: s['lines'].append(line_no)
                tr = TRACE.search(line)
                if tr:
                    direction, desc, code, sid, reply = tr.groups()
                    if direction == 'out' and desc == BROKER and code == '46' and sid:
                        sid = int(sid)
                        s = services.setdefault(sid, {'id': sid, 'name': names.get(sid, 'UNKNOWN'), 'requested': False,
                                                      'broker_replies': set(), 'statuses': set(), 'lines': []})
                        s['requested'] = True
                        s['broker_replies'].add(reply)
                    elif direction == 'out' and desc.startswith(('com.google.android.gms.', 'com.google.firebase.', 'com.google.android.c2dm.')):
                        interfaces.setdefault(desc, set()).add(reply)
            if '[WL-MODULE]' in line:
                m = re.search(r'query /(?:api|api_force_staging)/([A-Za-z0-9_.]+)', line)
                if m: modules.add(m[1])
                if re.search(r'\bversion\s*=\s*0\b', line): zero_module = True
                if 'query failed' in line or 'SecurityException' in line: negative_module = True
            if re.search(r"isn.t Play Protect certified|not Play Protect certified|This device is not certified|android/uncertified/warning",line,re.I):
                explicit_certification = True
                if len(events)<4: events.append({'line': line_no, 'event': 'explicit-certification-warning'})
        serial_services=[]
        for s in services.values():
            s['statuses'] = sorted(s['statuses']); s['broker_replies'] = sorted(s['broker_replies']); serial_services.append(s)
        ts=p.stat().st_mtime
        google[package].append({'round': tag, 'source': label(p), 'record_utc':utc(ts),
                                'after_reported_restriction_time':ts>=CUTOFF,
                                'has_play_trace':'[WL-PLAY-TRACE]' in text,
                                'services':sorted(serial_services,key=lambda s:s['id']),
                                'interfaces':{k:sorted(v) for k,v in interfaces.items()},
                                'module_queries':sorted(modules), 'module_version_zero_observed':zero_module,
                                'module_query_failure_observed':negative_module,
                                'explicit_certification_warning':explicit_certification, 'events':events})
        if n%250==0: print('Google logs:',n,'/',len(selected),flush=True)
    for rows in google.values():rows.sort(key=lambda r:r['record_utc'])
    print('Google evidence:',len(selected),'logs',len(google),'packages',flush=True)

    static = json.loads(read(OUT/'google-static-scan.json'))
    if len(static['per_app']) != static['available_archives'] or 'completed_utc' not in static:
        raise RuntimeError('Fresh APK scan has not finished; rerun collector when complete')
    old_actions = json.loads(read(C/'play-services-clients-20261113.json'))
    historical_gap_rows=[]
    apps=[]
    for rank in ranked:
        pkg=rank['appId']; scores=history.get(pkg,[]); latest=scores[-1] if scores else None
        scan_path=C/'map120'/pkg/'scan.json'; map_path=C/'map120'/pkg/'gap-map.json'
        scan=json.loads(read(scan_path)) if scan_path.exists() else None
        gap=json.loads(read(map_path)) if map_path.exists() else None
        open_rows=[]
        if gap:
            for row in gap['rows']:
                if row.get('verdict') == 'supplied':continue
                if row.get('shim_class') == 'C0':continue
                item={k:row.get(k,'') for k in ['id','category','item','verdict','shim_class','effort','oh_touchpoint','shim']}
                open_rows.append(item)
                historical_gap_rows.append({'rank':int(rank['rank']),'package':pkg,**item,
                                            'source':label(map_path),'baseline_commit':gap.get('provider',{}).get('westlake',{}).get('commit','')})
        g=google.get(pkg,[]); post=[r for r in g if r['after_reported_restriction_time']]
        refused=sorted({s['id'] for r in post for s in r['services'] if 9 in s['statuses']})
        explicit=any(r['explicit_certification_warning'] for r in post)
        before=[r for r in g if r['round']=='l8f']
        l8_exposure=sorted({s['id'] for r in before for s in r['services'] if s['requested'] and s['id'] not in ALLOW})
        non9=sorted({f"{s['name']} ({s['id']}):{v}" for r in g for s in r['services'] for v in s['statuses'] if v not in [0,9]})
        if explicit: policy='explicit-certification-warning-observed'
        elif refused: policy='service-invalid-9-observed-after-cutoff'
        elif any(r['has_play_trace'] for r in post): policy='post-cutoff-trace-without-status-9'
        else: policy='no-post-cutoff-play-trace'
        fresh=static['per_app'].get(pkg)
        families=sorted(fresh['families']) if fresh and fresh['status']=='ok' else []
        exp=explore.get(pkg,[]); check=checks.get(pkg,[])
        download=download_by_package.get(pkg,{})
        abi=scan['apk'].get('native_abi_status') if scan else None
        row={'rank':int(rank['rank']), 'package':pkg, 'title':rank['title'],'kind':rank['kind'],
             'chart':rank.get('best_chart',''), 'version':download.get('version_name') or (scan['apk'].get('version_name') if scan else None),
             'apk_scan_version':scan['apk'].get('version_name') if scan else None,
             'download_source':download.get('source') or source_rows.get(pkg,{}).get('source','missing'),
             'archive_available':fresh is not None,'static_scan_status':fresh['status'] if fresh else 'archive-unavailable',
             'abi_status':abi,'latest_score':latest,'best_ever_stage':STAGES[max(s['rung'] for s in scores)] if scores else 'unscored',
             'scored_rounds':len(scores),'drawing_rounds':sum(s['rung']==5 for s in scores),
             'exploration_records':len(exp),'latest_exploration':exp[-1] if exp else None,
             'functional_records':len(check),'latest_functional':check[-1] if check else None,
             'google_families':families,'service_actions_scan_20261113':old_actions.get(pkg),
             'google_evidence':g,'policy_observation':policy,'post_cutoff_log_count':len(post),
             'post_cutoff_status9_services':refused,'post_cutoff_status9_names':[names.get(s,str(s)) for s in refused],
             'l8f_nonallowlisted_service_requests':l8_exposure,
             'other_connection_errors':non9,
             'historical_gap_baseline':gap.get('provider',{}).get('westlake',{}).get('commit') if gap else None,
             'historical_gap_source':label(map_path) if gap else None,
             'historical_gap_count':len(open_rows),'historical_gaps':open_rows,
             'native_library_entries':(scan['apk'].get('native_library_entries', 0)
                                       if isinstance(scan['apk'].get('native_library_entries', 0), int)
                                       else len(scan['apk'].get('native_library_entries', []))) if scan else None}
        apps.append(row)
    write_json('apps-evidence.json',apps)
    with (OUT/'historical-api-gaps.csv').open('w',newline='') as f:
        w=csv.DictWriter(f,list(historical_gap_rows[0]),lineterminator='\n');w.writeheader();w.writerows(historical_gap_rows)
    write_json('source-hashes.json',list(provenance.values()))
    write_json('rounds.json',[{'round':tag,'packages':len(rows),'drawing':sum(x['rung']==5 for x in rows.values()),
                              'latest_record_utc':max(x['record_utc'] for x in rows.values())} for tag,rows in rounds.items()])
    write_json('score-history.json',dict(history))
    print('Complete ledger:',len(apps),'apps;',len(historical_gap_rows),'historical candidate rows',flush=True)
    print('Policy observations:',dict(Counter(r['policy_observation'] for r in apps)),flush=True)
    print('l8f exposure:',sum(bool(r['l8f_nonallowlisted_service_requests']) for r in apps),flush=True)


if __name__=='__main__':main()
