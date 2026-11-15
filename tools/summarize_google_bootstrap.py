#!/usr/bin/env python3
"""Compare external probe results without treating account login or a Binder as API success."""
import argparse
import json
from pathlib import Path


def summarize(baseline, handoff, restart=None):
    runs = [baseline, handoff] + ([restart] if restart else [])
    result = {'schema': 'westlake-google-comparison-v1', 'server_registration_proven': False,
              'production_ready': False, 'refresh_verification': 'requires_official_network_checkin_evidence',
              'maps_location_signin': 'not_tested'}
    expected_phases = ['baseline', 'after-handoff'] + (['after-restart'] if restart else [])
    if any(run.get('schema') != 'westlake-google-probe-v1' or run.get('phase') != phase
           for run, phase in zip(runs, expected_phases)):
        return dict(result, outcome='invalid_input')
    fields = ['run', 'manufacturer', 'model', 'fingerprint', 'sdk', 'gmsVersion']
    if any(field not in run or run[field] != baseline.get(field) for run in runs for field in fields):
        return dict(result, outcome='unmatched_build_or_profile')
    maps = [{service['serviceId']: service for service in run.get('services', [])} for run in runs]
    if any(set(services) != {51, 8, 93, 300} for services in maps):
        return dict(result, outcome='incomplete_probe')
    if any(services[51].get('outcome') != 'broker_accepted' for services in maps):
        return dict(result, outcome='control_failed')
    before, after = maps[:2]
    blocked = [i for i in [8, 93, 300] if before[i].get('brokerStatus') == 9]
    result['baseline_error9_services'] = blocked
    result['after_error9_services'] = [i for i in [8, 93, 300] if after[i].get('brokerStatus') == 9]
    result['newly_accepted_brokers'] = [i for i in blocked if after[i].get('outcome') == 'broker_accepted']
    result['app_set_api_success'] = after[300].get('appSet', {}).get('outcome') == 'api_success'
    result['restart_app_set_api_success'] = (
        maps[2][300].get('appSet', {}).get('outcome') == 'api_success' if restart else None)
    if not blocked:
        outcome = 'baseline_did_not_reproduce_error9'
    elif result['after_error9_services']:
        outcome = 'restriction_persists_for_sampled_services'
    elif result['app_set_api_success'] and result['newly_accepted_brokers']:
        outcome = 'sampled_improvement_refresh_and_durability_unverified'
    else:
        outcome = 'no_complete_api_improvement_demonstrated'
    return dict(result, outcome=outcome)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', required=True, type=Path)
    parser.add_argument('--handoff', required=True, type=Path)
    parser.add_argument('--restart', type=Path)
    args = parser.parse_args()
    read = lambda p: json.loads(p.read_text()) if p else None
    print(json.dumps(summarize(read(args.baseline), read(args.handoff), read(args.restart)), indent=2))
