"""Render every ranked app with explicit evidence strength and reproducible counts."""
from collections import Counter, defaultdict
import csv
import datetime as dt
import hashlib
import html
import json
from pathlib import Path

BASE = Path('/home/dspfac/a2hlab')
OUT = BASE / 'reports/hk-top1000-audit-20261115'
APPS = json.loads((OUT/'apps-evidence.json').read_text())
STATIC = json.loads((OUT/'google-static-scan.json').read_text())
ROUNDS = json.loads((OUT/'rounds.json').read_text())
HISTORY = json.loads((OUT/'score-history.json').read_text())
NAMES = json.loads((OUT/'gms-service-names.json').read_text())
CUTOFF = '2026-11-14T15:36:46+00:00'
FEATURES = {
    'gms:sign-in': ('Google sign-in / identity', {91,212,343,381}),
    'gms:location': ('Google location', {23}),
    'gms:maps': ('Google Maps SDK', set()),
    'gms:ads-api-excluding-identifier': ('Ads API types excluding Advertising ID', {8,116}),
    'gms:ads-identifier': ('Advertising ID', set()),
    'play:integrity': ('Play Integrity', set()),
    'gms:safetynet': ('Legacy SafetyNet', {45}),
    'gms:wallet-pay': ('Google Wallet / Pay', {4,198}),
    'play:billing': ('Play Billing', set()),
    'fcm': ('Firebase Cloud Messaging', set()),
    'recaptcha': ('reCAPTCHA', {380}),
    'gms:games': ('Play Games', {1}),
}
FLOW = {
    'gms:sign-in':'Google account chooser, consent, token return and a subsequent authenticated request',
    'gms:location':'fresh location fix, permission denial, updates and background delivery',
    'gms:maps':'map tiles, camera movement and user-location overlay',
    'gms:ads-api-excluding-identifier':'ad loading, display, close and rewarded-ad callback where offered',
    'gms:ads-identifier':'Advertising ID availability and consent/opt-out behavior',
    'play:integrity':'Integrity token plus the app backend verdict and enforcement',
    'gms:safetynet':'legacy attestation response and backend handling',
    'gms:wallet-pay':'wallet provisioning or a sandbox payment flow',
    'play:billing':'product query, sandbox purchase, restore and acknowledgement',
    'fcm':'push receipt in foreground, background and after process restart',
    'recaptcha':'a challenge/token accepted by the app backend',
    'gms:games':'game sign-in, cloud save and resume',
}


def jwrite(name, obj):
    (OUT/name).write_text(json.dumps(obj, ensure_ascii=False, indent=2)+'\n')


def day(s): return s[:19].replace('T',' ')+' UTC'


def md(s): return str(s).replace('|','\\|').replace('\n',' ')


def services(app, post=None, round_name=None):
    result = defaultdict(lambda: {'statuses':set(), 'requested':False})
    for e in app['google_evidence']:
        if post is not None and e['after_reported_restriction_time'] != post: continue
        if round_name is not None and e['round'] != round_name: continue
        for s in e['services']:
            result[s['id']]['statuses'].update(s['statuses'])
            result[s['id']]['requested'] |= s['requested']
    return result


def api_text(app, post):
    svc = services(app,post)
    if not svc: return 'No service-broker callback/request evidence in the selected '+('post-cutoff' if post else 'earlier')+' logs.'
    return '; '.join(f"{NAMES.get(str(i), 'UNKNOWN')} ({i}): statuses {','.join(map(str,sorted(s['statuses']))) or 'none recorded'}" for i,s in sorted(svc.items())) + '. Status 0 proves a service connection only.'


def enrich(a):
    s,e,f = a['latest_score'],a['latest_exploration'],a['latest_functional']
    stage = s['stage'] if s else 'unscored'
    if s:
        running = f"Latest saved launch: {stage}, round {s['round']}, {day(s['record_utc'])} (file timestamp). "
        running += ('App drawing was accepted by the saved scorer; ongoing usability is unverified.' if stage=='drawing'
                    else 'The saved scorer did not accept a sustained app drawing result in this run.')
        if a['best_ever_stage']=='drawing' and stage!='drawing':
            running += ' An earlier run did draw; different builds/runs prevent calling this a confirmed regression.'
        if s['annotations']: running += ' Scorer note: '+'; '.join(s['annotations'])
    else:
        running = 'No scored board launch record. '+('A local archive exists.' if a['archive_available'] else 'No local archive was available for this audit.')
    blocked=[]
    if s and s['rung']<5: blocked.append('Latest saved launch finding: '+(s['blocker'] or 'no sustained drawing result; cause not classified')+'. This is a historical observation, not a verified current-build root cause.')
    if a['post_cutoff_status9_names']:
        blocked.append('Observed after the reported restriction time: SERVICE_INVALID (9) for '+', '.join(a['post_cutoff_status9_names'])+'. This confirms service refusal, not failure of the whole app or its every feature.')
    if not blocked: blocked.append('No launch blocker or post-cutoff status-9 refusal established by the selected evidence. This is not a full compatibility pass.')
    if a['abi_status']=='target-abi-unavailable': blocked.append('The older APK scan reports no target ABI; a compatible archive is needed before drawing conclusions about the runtime.')
    if e and e['status']=='ended-during-exploration': blocked.append(f"An older exploration ended at {e['died_at']} s in {e['round']}; the detector does not by itself distinguish crash, intentional exit or app handoff.")
    tested=[f"{a['scored_rounds']} distinct scored rounds; {a['drawing_rounds']} accepted drawing results. These are saved historical tests, not reruns in this audit."]
    if e:
        tested.append(f"Latest exploration {e['round']} ({day(e['record_utc'])}): {e['status']}; {e['seconds']} s, {e['steps']} steps, {e['screens']} reported screens, {e['taps']} taps, {e['scrolls']} scrolls, {e['empty_dumps']} empty hierarchy dumps. Screen count is a harness metric, not manually verified distinct working screens.")
    else: tested.append('No retained interaction-exploration record.')
    if f:
        checks='; '.join(f"{i['do']}: {'pass' if i['ok'] else 'fail'}"+(f" [{i.get('matched') or i.get('text')}]" if i.get('matched') or i.get('text') else '') for i in f['steps'])
        tested.append(f"Latest scripted check {f['round']} ({day(f['record_utc'])}): {f['passed']}/{f['of']}. {checks}. These are narrow UI assertions; they do not certify the app's business flow.")
    else: tested.append('No retained feature-specific scripted check.')
    tested.append(f"Fresh DEX scan: {a['static_scan_status']}; all available classes*.dex and nested APK splits were inspected for SDK type names.")
    gaps=['No complete current-build acceptance test or version-matched post-restriction business-flow result is established by this evidence set.']
    if not a['archive_available']: gaps.append('Acquire and identify an installable archive before static or runtime assessment.')
    elif not s: gaps.append('Install and capture a first scored launch; an available archive does not establish installability.')
    if not a['post_cutoff_log_count']: gaps.append('No post-cutoff app log in the audited set; current Google-policy behavior is untested.')
    elif not any(x['after_reported_restriction_time'] and x['has_play_trace'] for x in a['google_evidence']): gaps.append('Post-cutoff raw logs exist but contain no Play trace; Google-policy behavior remains unobserved.')
    flows=[FLOW[k] for k in FEATURES if k in a['google_families']]
    if flows: gaps.append('SDK-triggered test candidates (presence alone does not prove the feature is exposed): '+'; '.join(flows)+'.')
    else: gaps.append('Absence of the scanned Google types does not prove independence from Google, web flows, obfuscated SDKs or native code.')
    if e and e['status']=='mostly-empty-view-trees': gaps.append('Most hierarchy dumps were empty. Validate with pixels and explicit actions; an empty accessibility tree is not proof of an empty rendered screen.')
    gaps.append('Core app flow, real network data, background/resume, push, media/camera where applicable, memory, CPU and battery behavior still require explicit acceptance evidence.')
    if a['historical_gap_source']:
        gaps.append(f"Build-120 static baseline lists {a['historical_gap_count']} non-supplied/non-C0 candidate rows. They are stale hypotheses, not {a['historical_gap_count']} confirmed current API failures.")
    if not a['archive_available']: next_test='Acquire an allowed, target-compatible app build, pin its hash, then install and launch.'
    elif not s: next_test='Resolve archive/ABI/install prerequisites, then record a first launch with package and framework build hashes.'
    elif s['rung']<5: next_test='Reproduce the latest launch finding on the frozen candidate build; symbolize crashes or capture the stall before feature tests.'
    elif a['post_cutoff_status9_services']: next_test='Repeat the refused service and its user-visible flow after recording Play restriction state; compare fallback behavior and a permitted service control.'
    elif not e: next_test='Run a deterministic onboarding and core-flow interaction test, then exercise the listed Google dependencies.'
    else: next_test='Rerun the core app flow and listed Google-dependent features on the current frozen build; record explicit pass/fail assertions.'
    test_level='scripted UI checks' if f else 'exploration' if e else 'launch only' if s else 'no scored launch'
    return {**a, 'stage':stage,'test_level':test_level,'running_description':running,
            'blocked_description':' '.join(blocked),'tested_description':' '.join(tested),
            'gaps_description':' '.join(gaps),'next_test':next_test,
            'earlier_api_observations':api_text(a,False),'post_cutoff_api_observations':api_text(a,True),
            'historical_gap_categories':dict(Counter(g['category'] for g in a['historical_gaps']))}


def main():
    apps=[enrich(a) for a in APPS]
    assert len(apps)==1000 and len({a['package'] for a in apps})==1000
    assert {a['rank'] for a in apps}==set(range(1,1001))
    assert STATIC['available_archives']==982 and len(STATIC['per_app'])==982
    assert all(r['status']=='ok' for r in STATIC['per_app'].values())
    assert all(x['stable_during_read'] for x in json.loads((OUT/'source-hashes.json').read_text()))
    assert all(a['google_families']==sorted(STATIC['per_app'][a['package']]['families']) for a in apps if a['archive_available'])
    latest=Counter(a['stage'] for a in apps)
    expl=Counter(a['latest_exploration']['status'] if a['latest_exploration'] else 'not-tested' for a in apps)
    post_apps=[a for a in apps if a['post_cutoff_log_count']]
    refused=[a for a in apps if a['post_cutoff_status9_services']]
    service_rows=[]
    for sid in sorted({sid for a in apps for sid in services(a,round_name='l8f')}):
        cohort=[services(a,round_name='l8f').get(sid) for a in apps]
        service_rows.append({'id':sid,'name':NAMES.get(str(sid),'UNKNOWN'),
            'l8f_requested_apps':sum(bool(s and s['requested']) for s in cohort),
            'l8f_status0_apps':sum(bool(s and 0 in s['statuses']) for s in cohort),
            'l8f_nonzero_apps':sum(bool(s and any(v!=0 for v in s['statuses'])) for s in cohort),
            'post_cutoff_status9_apps':sum(sid in a['post_cutoff_status9_services'] for a in apps)})
    feature_rows=[]
    for key,(title,ids) in FEATURES.items():
        feature_rows.append({'family':key,'feature':title,'static_type_presence_apps':STATIC['counts'][key],
            'broker_service_ids':sorted(ids),
            'l8f_requested_apps':sum(any(sid in ids and v['requested'] for sid,v in services(a,round_name='l8f').items()) for a in apps) if ids else None,
            'post_cutoff_status9_apps':sum(bool(ids.intersection(a['post_cutoff_status9_services'])) for a in apps) if ids else None})
    summary={'generated_utc':dt.datetime.now(dt.timezone.utc).isoformat(),'ranked_apps':1000,
        'archive_scanned':982,'archive_unavailable':18,'latest_saved_launch':dict(latest),
        'ever_drawing':sum(a['best_ever_stage']=='drawing' for a in apps),
        'exploration_latest':dict(expl),'exploration_records':sum(a['exploration_records'] for a in apps),
        'scripted_check_apps':sum(bool(a['latest_functional']) for a in apps),
        'scripted_check_records':sum(a['functional_records'] for a in apps),
        'latest_scripted_assertions_passed':sum(a['latest_functional']['passed'] for a in apps if a['latest_functional']),
        'latest_scripted_assertions_total':sum(a['latest_functional']['of'] for a in apps if a['latest_functional']),
        'post_cutoff_logged_apps':len(post_apps),'post_cutoff_logs':sum(a['post_cutoff_log_count'] for a in apps),
        'post_cutoff_play_trace_apps':sum(any(e['after_reported_restriction_time'] and e['has_play_trace'] for e in a['google_evidence']) for a in apps),
        'post_cutoff_status9_apps':len(refused),'post_cutoff_status9_also_drawing':sum(a['stage']=='drawing' for a in refused),
        'google_raw_logs_parsed':sum(len(a['google_evidence']) for a in apps),
        'source_reads':len(json.loads((OUT/'source-hashes.json').read_text())),
        'l8f_nonallowlisted_service_request_apps':sum(bool(a['l8f_nonallowlisted_service_requests']) for a in apps),
        'historical_candidate_rows':sum(a['historical_gap_count'] for a in apps),
        'historical_candidate_unique_ids':len({g['id'] for a in apps for g in a['historical_gaps']}),
        'blocker_categories':dict(Counter(a['latest_score']['blocker_category'] or 'unclassified' for a in apps if a['latest_score'] and a['latest_score']['rung']<5)),
        'features':feature_rows,'broker_services':service_rows,'full_business_flow_acceptance_established':False}
    jwrite('summary.json',summary)
    jwrite('apps.json',apps)
    fields=['rank','package','title','kind','version','stage','test_level','archive_available','abi_status',
            'scored_rounds','drawing_rounds','best_ever_stage','last_round','last_record_utc','last_blocker',
            'post_cutoff_log_count','post_cutoff_status9_names','google_families','historical_gap_count',
            'running_description','blocked_description','tested_description','gaps_description','next_test',
            'earlier_api_observations','post_cutoff_api_observations','score_source','score_line','historical_gap_source']
    with (OUT/'apps.csv').open('w',newline='') as f:
        w=csv.DictWriter(f,fields,lineterminator='\n');w.writeheader()
        for a in apps:
            row={k:a.get(k,'') for k in fields};s=a['latest_score'] or {}
            row.update(last_round=s.get('round',''),last_record_utc=s.get('record_utc',''),last_blocker=s.get('blocker',''),
                       score_source=s.get('score_source',''),score_line=s.get('score_line',''))
            for k in ['google_families','post_cutoff_status9_names']:row[k]='; '.join(a[k])
            w.writerow(row)
    for filename,rows in [('google-features.csv',feature_rows),('google-broker-services.csv',service_rows)]:
        with (OUT/filename).open('w',newline='') as f:
            w=csv.DictWriter(f,list(rows[0]),lineterminator='\n');w.writeheader();w.writerows(rows)
    render_markdown(apps,summary)
    render_html(apps,summary)
    verification={'rank_unique':1000,'package_unique':1000,'archive_scans_ok':982,'missing_archives':18,
                  'stable_source_reads':True,'fresh_scan_matches_every_ledger_row':True,
                  'generated_utc':dt.datetime.now(dt.timezone.utc).isoformat()}
    jwrite('validation.json',verification)
    hashes={}
    for p in sorted(OUT.iterdir()):
        if p.is_file() and p.name!='artifact-hashes.json':hashes[p.name]=hashlib.sha256(p.read_bytes()).hexdigest()
    for p in sorted((BASE/'research_notes/hk-top1000-audit-20261115').iterdir()):
        if p.is_file():hashes['../../research_notes/hk-top1000-audit-20261115/'+p.name]=hashlib.sha256(p.read_bytes()).hexdigest()
    jwrite('artifact-hashes.json',hashes)
    print(json.dumps({k:v for k,v in summary.items() if k not in ['features','broker_services']},indent=2))


def render_markdown(apps,s):
    feature_table='\n'.join(f"| {r['feature']} | {r['static_type_presence_apps']} | {r['l8f_requested_apps'] if r['l8f_requested_apps'] is not None else 'not measured here'} | {r['post_cutoff_status9_apps'] if r['post_cutoff_status9_apps'] is not None else 'not measured here'} |" for r in s['features'])
    blocked_table='\n'.join(f"| {a['rank']} | {md(a['title'])} | `{a['package']}` | {a['stage']} | {', '.join(a['post_cutoff_status9_names'])} |" for a in apps if a['post_cutoff_status9_services'])
    checks_table='\n'.join(f"| {md(a['title'])} | {a['stage']} | {a['latest_functional']['passed']}/{a['latest_functional']['of']} | {md('; '.join(x['do']+': '+str(x.get('matched') or x.get('text') or '') for x in a['latest_functional']['steps']))} |" for a in apps if a['latest_functional'])
    missing='\n'.join(f"| {a['rank']} | {md(a['title'])} | `{a['package']}` | {'archive missing' if not a['archive_available'] else 'archive present; '+str(a['abi_status'])} |" for a in apps if not a['latest_score'])
    source_lines=f"""# Hong Kong top-1,000 app evidence audit — 2026-11-15

All 1,000 ranked entries have an individual record. **785 have an accepted drawing result in their latest saved launch, 193 have a lower result, and 22 have no scored launch. This is not evidence that 785 apps fully work today.** The latest launch results mix builds and dates from November 6–14. The fresh work in this audit is an APK dependency scan and evidence reconciliation, not a new 1,000-app board run.

Open [the searchable offline report](index.html), [all 1,000 detailed descriptions](all-apps.md), or [the complete CSV](apps.csv). [apps.json](apps.json) carries descriptions plus structured evidence; [apps-evidence.json](apps-evidence.json) is the underlying ledger.

## Scope and meaning of “running”

The list is the project's frozen 1,000-package HK corpus: 706 apps and 294 games, assembled from 102 HK Play charts sampled on October 23. Its rank is a combined chart score, not a current market-share or usage ranking. Every listed rank is accounted for; 982 local archives were freshly scanned, including nested APK splits. Eighteen have no local archive. The download registry has 980 records, which is a different count from available archives.

| Latest saved launch result | Apps | What it establishes |
|---|---:|---|
| drawing | 785 | The saved scorer accepted app drawing in that run; this can include screenshot/hilog overrides |
| view | 56 | A view was reached, but the final app drawing result was not accepted |
| activity | 2 | Activity creation reached; no accepted drawing result |
| bound | 16 | Application bind reached; no accepted activity/drawing result |
| runtime-init | 119 | Runtime initialization reached; app progressed no further in that score |
| unscored | 22 | No retained scored launch |

829 apps drew at least once historically. Forty-four of those have a lower latest result. Builds, sample duration and test paths differ, so this is not a controlled regression count. Drawing, Google refusal and interaction failure are overlapping dimensions: **{s['post_cutoff_status9_also_drawing']} of the 26 apps with observed Google refusals also have a latest drawing score.**

## What was actually tested

- 978 packages have saved launch scores across 301 distinct scored rounds. Repeated progress tables within a round are deduplicated to the last row per package.
- 515 packages have exploration records: latest result survived for 315, ended during exploration for 176, and had mostly empty view trees for 24. The remaining 485 have no exploration record. Exploration taps do not prove a meaningful user journey; empty trees do not prove empty pixels.
- Only six packages have retained feature-specific scripted checks. Their latest checks total 16/16 assertions on November 11. These assertions are limited UI markers/actions, not login, payment, playback or end-to-end acceptance.
- This audit parsed {s['google_raw_logs_parsed']} selected raw app logs: the latest scored log per app, the complete l8f sample, and retained logs after the reported restriction time. Earlier historical raw Google traces outside that selection were not exhaustively rescanned.
- No complete, version-matched, current-build business-flow acceptance is established for the corpus by these records.

| App | Latest launch score | Latest scripted assertions | Assertion scope |
|---|---|---:|---|
{checks_table}

YouTube illustrates why these columns stay separate: its checks passed 5/5, while its saved final score is `view`, with a stall and a note that the app left/died before the final screenshot. Spotify's 2/2 includes reaching “Sign up free”; it does not establish music playback. Uber's 1/1 does not establish location or booking a ride.

## Google restriction: observed facts and limits

The board research reports an uncertified-state expiry at **2026-11-14 15:36:46 UTC**. The l8f cohort ran earlier, 05:08–13:14 UTC, and contains 335 packages, 308 drawing. Later s251a/s251b permission A/B rounds cover 23 packages each, with 21/20 drawing. They are targeted samples, not a new full-corpus score.

There are **{s['post_cutoff_logs']} retained post-cutoff logs for {s['post_cutoff_logged_apps']} ranked packages**. Twenty-six packages contain at least one callback with **ConnectionResult 9, SERVICE_INVALID**. Telegram X's later logs have only unidentified-interface trace activity and no identified service-broker callback, so its Google service behavior remains unobserved. The count is **26 observed affected packages**, not 26% of the corpus and not an estimate of population prevalence. Individual app logs do not themselves contain the certification warning text detected by this parser; the attribution to the board's uncertified gate comes from the separate board/code investigation.

Google defines status 9 as invalid Play services/authenticity, not a universal code for certification failure. The association with this board's restriction therefore relies on the independently recorded board state. Status 0 proves connection to a service, not successful business output. [Google ConnectionResult reference](https://developers.google.com/android/reference/com/google/android/gms/common/ConnectionResult). Device certification is also distinct from Play Protect malware scanning. [Google certification guidance](https://support.google.com/googleplay/answer/7165974).

The strongest post-cutoff counts are MEASUREMENT 26, APP_SET_ID 26, GASS 20 and ADMOB 17. The targeted set also has one identity-sign-in refusal. There is no sampled post-cutoff location-service refusal here; that means **not observed**, not “location works.” Cached/install-time modules can still work: PR 166 records Cronet loading, so “all Google modules are blocked” is too broad.

### Fresh SDK scan versus runtime evidence

| Feature family | APKs with matching DEX types, of 982 | l8f apps requesting related broker IDs, of 335 | Post-cutoff apps with related status 9, of 27 logged |
|---|---:|---:|---:|
{feature_table}

These columns are separate measurements, not a funnel over matched feature invocations. Service-ID mappings are explicitly listed in [google-features.csv](google-features.csv). Maps, FCM, Billing, Integrity and Advertising ID also use providers/direct interfaces or other paths, so no broker count is supplied where this parser cannot establish one. Zero broker refusals does not establish feature success. The Ads count excludes the identifier-only namespace; the broader `gms:ads` prefix matches {STATIC['counts']['gms:ads']} archives and should not be called a count of apps that display ads.

The very high sign-in presence count includes SDK type references bundled transitively, such as GoogleSignInAccount. Type presence is not a reachable call, enabled UI feature or confirmed dependency at runtime. Reflection, obfuscation, web flows and native code can evade the scan. Fresh DEX hashes and example descriptors are in [google-static-scan.json](google-static-scan.json). Counts may overlap and must not be summed.

### The quoted “15%” is not a validated block rate

Within l8f, 64/335 apps requested sign-in IDs 91/212 or location ID 23. Adding ADMOB 8 and GASS 116 gives **133/335**, which is 13.3% of all 1,000 ranks and only an observed startup-exposure floor for those exact services. Adding games, wallet and other services changes it again. Considering every service outside the reported allowlist yields **320/335**, dominated by measurement and app-set/telemetry requests. Those are potential exposures under the later gate, not 320 demonstrated broken apps. The 15% claim cannot be reproduced without specifying the exact API set and denominator.

Registering the board cannot be assumed to solve every gap. PR 168's board test shows the corrected Gservices replacement semantics, yet Google still sends `uncertified_status=3`. Registration outcome, service access and Play Integrity backend acceptance each need separate evidence.

### All 26 packages with observed post-cutoff service refusal

| Rank | App | Package | Latest launch | Refused services across post-cutoff logs |
|---:|---|---|---|---|
{blocked_table}

## Launch blockers and API adaptation gaps

The 193 non-drawing latest scores comprise 124 native-crash, 16 stall, 12 app-framework, 10 self-exit, 9 no-frame, 2 native-loading, 2 self-finish, 2 self-kill, 1 native-upcall, 1 native-symbol and 14 unclassified results. These are saved scorer labels, not newly verified root causes. In particular, **124 native crashes does not mean 124 Bionic/musl defects**; causes may include app code, runtime, graphics, bridge behavior or external libraries.

The old map120 scan contains **38,030 non-supplied, non-C0 candidate rows across apps, covering 553 unique candidate IDs**. These include framework natives, package manager, system services, external dependencies and native symbols. They are from baseline commit `e8d4b20272fff46b97c49737129f373ff43a1f34`, not today's implementation. One row may cover an API family or policy, and the same row recurs across many apps. Neither number is a count of APIs newly implemented, APIs currently missing, or apps currently broken. Every row is retained in [historical-api-gaps.csv](historical-api-gaps.csv); per-app records show categories and source references.

Current cross-app implementation gaps, based on open PR descriptions retrieved for this audit (author-reported board results, not independently rerun here):

| Area | Recorded implementation/test | What remains unproven |
|---|---|---|
| Location — [PR 164](https://github.com/a2hlab/westlake/pull/164) | Requests reach OH location; permission/switch paths exercised | No fix delivered to an Android listener; no network provider and vendor GNSS failure on this board. GNSS also disrupted WiFi; fallback disables GNSS use |
| Notifications — [PR 165](https://github.com/a2hlab/westlake/pull/165) | Eight probe notifications shown/cancelled in OH | Actions/content intents, icons/pictures and dismissal feedback; this does not prove FCM delivery |
| Play modules — [PR 166](https://github.com/a2hlab/westlake/pull/166) | Read-only descriptors/copies checked; install-time Cronet module loaded | Downloaded ads module end-to-end loading remains gated; copy mode is opt-in |
| Permissions — [PR 162](https://github.com/a2hlab/westlake/pull/162) | 23-app phase-A A/B, 21 versus 20 drawing | Enforcement switch remains off; full-corpus A/B and per-app runtime permission semantics remain incomplete |
| Gservices — [PR 168](https://github.com/a2hlab/westlake/pull/168) | Full replacement removed stale keys on build 264 | Server still returns uncertified status 3; a lifted restriction has not been demonstrated |

These PRs were OPEN at retrieval; availability on a developer board does not mean merged default support. Raw snapshots and head commits are in [implementation-pr-snapshots.json](implementation-pr-snapshots.json).

## All unscored packages

| Rank | App | Package | Evidence gap |
|---:|---|---|---|
{missing}

## How to close the gaps

1. Freeze the candidate Westlake build, Play services version, permission/module switches, APK hashes and board Gservices state. Capture before/after state for any registration experiment.
2. Rerun the 978 available previously scored apps on that one build; acquire the missing 18 and resolve the four archive-present unscored entries. Score launch separately from core behavior.
3. Start deterministic feature tests with the 26 refusal-observed apps, the l8f sign-in/location group and finance/transport/messaging apps. Test sign-in tokens, location fixes, map tiles, push after process death, ad callbacks, sandbox billing and backend integrity decisions separately.
4. Reproduce the 193 launch findings, prioritize the 124 native crashes with symbolized evidence, and rescan API gaps against the candidate implementation. Do not treat the old map120 counts as the current backlog.
5. Add full core-flow assertions for the 994 apps without retained scripted checks, plus interaction coverage for the 485 with no exploration. Measure memory, CPU, battery and background behavior in controlled longer runs; the launch corpus does not answer those questions.

## Evidence provenance and reproducibility

Input root aliases in the files: `corpus/` = `/home/dspfac/a2hlab/source-closure/inputs/corpus-hk`; `logs/` = `/home/dspfac/a2hlab/source-closure/inputs/logs` (an existing symlink into the external-disk log archive); `audit/` = this report directory. Source files were read only; no board session was started or changed. The collector retained source SHA-256, size, file timestamp and stability in [source-hashes.json](source-hashes.json). All {s['source_reads']:,} source reads were stable during each read; this is a rolling evidence snapshot, not an atomic snapshot of the live project. File mtimes are ordering proxies, not trusted launch timestamps. Package identities join the data; current registry versions are not proof of the APK version used in an older round.

The DEX scan completed at {STATIC['completed_utc']}. Runtime traces and scores are historical. SDK namespace rules are preserved in the scan output; older November 13 intent-action scans use a different taxonomy and are not directly comparable. Service-ID names come from harness commit `6a22d743fa487797943199355990d712977b4c95`. The restriction-state source is `research_notes/Play certification workarounds and registration/coordinator_verified_facts.md`, supplemented by the retrieved PR 166/168 board descriptions.

Scripts are under `../../research_notes/hk-top1000-audit-20261115/`: `scan_google.py`, `collect_evidence.py`, then `render_report.py`. Run them in that order. [artifact-hashes.json](artifact-hashes.json) identifies the generated snapshot and scripts. [validation.json](validation.json) records reconciliation checks. No raw account identifiers, coordinates or raw UI hierarchy text are copied into this report.
"""
    (OUT/'README.md').write_text(source_lines)
    with (OUT/'all-apps.md').open('w') as f:
        f.write('# All 1,000 app records\n\nHistorical launch scores and fresh static dependencies are separate evidence. Read [methodology and limitations](README.md) first. No record is a full current-build compatibility certification.\n\n')
        for a in apps:
            f.write(f"## {a['rank']}. {a['title']}\n\nPackage: `{a['package']}`. Registry/scan version: `{a['version'] or 'unknown'}` (not tied to old run versions).\n\n")
            for key,label in [('running_description','Running / launch'),('blocked_description','Observed blockers'),('tested_description','Tests'),('earlier_api_observations','Earlier Google API evidence'),('post_cutoff_api_observations','Later Google API evidence'),('gaps_description','Remaining gaps'),('next_test','Next test')]:
                f.write(f"**{label}:** {a[key]}\n\n")
            f.write('**Static Google type families:** '+(', '.join(a['google_families']) or 'none observed / archive unavailable')+'.\n\n')
            f.write('**Build-120 historical candidate categories:** '+(', '.join(f'{k}: {v}' for k,v in a['historical_gap_categories'].items()) or 'none')+'. Full rows are in historical-api-gaps.csv.\n\n')
            sources=[]
            if a['latest_score']:sources.append(a['latest_score']['score_source']+':'+str(a['latest_score']['score_line']))
            if a['latest_exploration']:sources.append(a['latest_exploration']['source'])
            if a['latest_functional']:sources.append(a['latest_functional']['source'])
            sources.extend(g['source'] for g in a['google_evidence'])
            f.write('**Evidence sources:** '+('; '.join('`'+x+'`' for x in sources) or 'no runtime evidence')+'.\n\n')
    detailed = OUT/'all-apps.md'
    detailed.write_text(detailed.read_text().rstrip()+'\n')


def render_html(apps,s):
    # Keep the standalone UI compact; full candidate rows remain in JSON/CSV.
    compact=[]
    for a in apps:
        row={k:v for k,v in a.items() if k not in ['historical_gaps','google_evidence','service_actions_scan_20261113']}
        row['google_log_sources']=[{'round':g['round'],'source':g['source'],'date':g['record_utc'],
                                   'service_callback_lines':{str(x['id']):x['lines'] for x in g['services']}} for g in a['google_evidence']]
        row['historical_examples']=a['historical_gaps'][:12]
        compact.append(row)
    template=Path(__file__).with_name('report_template.html').read_text()
    data=json.dumps({'apps':compact,'summary':s},ensure_ascii=False,separators=(',',':')).replace('<','\\u003c')
    (OUT/'index.html').write_text(template.replace('__AUDIT_DATA__',data))


if __name__=='__main__':main()
