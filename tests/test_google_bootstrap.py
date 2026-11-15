import copy
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('comparison', Path(__file__).parents[1] / 'tools/summarize_google_bootstrap.py')
comparison = importlib.util.module_from_spec(spec)
spec.loader.exec_module(comparison)


def fixture(phase, blocked):
    return dict(schema='westlake-google-probe-v1', run='test01', phase=phase, manufacturer='HopeRun', model='DAYU200',
                fingerprint='test', sdk=34, gmsVersion=244034029, services=[
                    dict(serviceId=i, brokerStatus=9 if blocked and i != 51 else 0,
                         outcome='broker_rejected' if blocked and i != 51 else 'broker_accepted')
                    for i in [51, 8, 93, 300]])


class ComparisonTest(unittest.TestCase):
    def test_persistent_restriction_is_not_login_success(self):
        result = comparison.summarize(fixture('baseline', True), fixture('after-handoff', True))
        self.assertEqual(result['outcome'], 'restriction_persists_for_sampled_services')
        self.assertFalse(result['server_registration_proven'])

    def test_broker_success_is_not_api_success(self):
        result = comparison.summarize(fixture('baseline', True), fixture('after-handoff', False))
        self.assertEqual(result['outcome'], 'no_complete_api_improvement_demonstrated')

    def test_api_success_does_not_prove_fresh_policy_or_other_features(self):
        after = fixture('after-handoff', False)
        after['services'][-1]['appSet'] = dict(outcome='api_success')
        restart = copy.deepcopy(after); restart['phase'] = 'after-restart'
        result = comparison.summarize(fixture('baseline', True), after, restart)
        self.assertTrue(result['restart_app_set_api_success'])
        self.assertEqual(result['outcome'], 'sampled_improvement_refresh_and_durability_unverified')
        self.assertFalse(result['server_registration_proven'])

    def test_changed_profile_is_not_matched_evidence(self):
        after = fixture('after-handoff', False); after['manufacturer'] = 'different'
        self.assertEqual(comparison.summarize(fixture('baseline', True), after)['outcome'], 'unmatched_build_or_profile')

    def test_timeout_is_not_a_policy_rejection(self):
        after = fixture('after-handoff', False)
        after['services'][0]['outcome'] = 'request_timeout'
        self.assertEqual(comparison.summarize(fixture('baseline', True), after)['outcome'], 'control_failed')

    def test_no_original_block_is_not_a_fixed_block(self):
        self.assertEqual(comparison.summarize(fixture('baseline', False), fixture('after-handoff', False))['outcome'],
                         'baseline_did_not_reproduce_error9')


if __name__ == '__main__':
    unittest.main()
