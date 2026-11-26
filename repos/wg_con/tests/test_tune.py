"""Small fake encoder tests the CCF-based sequential search and checkpoints."""

import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import tune


def write_ccf(path, n=4, m=3):
    records = 2
    file_bytes = 220
    header = bytearray(160)
    struct.pack_into(
        "<8sIIII9Q", header, 0, tune.CCF_MAGIC, 1, 160, 1, 0,
        n, m, records, 5, 160, records + 1, 160 + 8 * (records + 1),
        file_bytes - 160 - 8 * (records + 1), file_bytes,
    )
    header[96:128] = bytes(range(32))
    header[128:160] = bytes([7]) * 32
    path.write_bytes(header + bytes(file_bytes - 160))


FAKE_ENCODER = r'''#!/usr/bin/env python3
import argparse
import json
import os
from pathlib import Path
import struct
import sys

if sys.argv[1:] == ['--version']:
    print('fake encoder 3')
    sys.exit(0)
parser = argparse.ArgumentParser()
parser.add_argument('--ccf')
parser.add_argument('--basename')
parser.add_argument('--order')
parser.add_argument('--order-cache')
parser.add_argument('--window', type=int)
parser.add_argument('--max-ref-count')
parser.add_argument('--min-interval-length', type=int)
parser.add_argument('--bvgraphz', action='store_true')
for field in ('outdegrees', 'references', 'blocks', 'intervals', 'residuals'):
    parser.add_argument('--' + field)
args = parser.parse_args()
key = args.order.replace('-', '_') + '_w' + str(args.window)
with open(os.environ['WG_FAKE_SCENARIO']) as source:
    scenario = json.load(source)
with open(os.environ['WG_FAKE_CALLS'], 'a') as destination:
    destination.write(key + '\n')
expected_options = scenario.get('expected_options', {}).get(key, {})
if (args.max_ref_count != str(expected_options.get('max_ref_count', 'unlimited')) or
        args.bvgraphz != expected_options.get('bvgraphz', False)):
    sys.exit(13)
if key in scenario.get('fail_before_cache', []):
    print('failed before computing order', file=sys.stderr)
    sys.exit(17)
if args.order == 'bfs-llp':
    if not args.order_cache:
        sys.exit(15)
    cache = Path(args.order_cache)
    if not cache.exists():
        cache.write_bytes(struct.pack('<4I', 0, 1, 2, 3))
        with open(os.environ['WG_FAKE_CACHE_BUILDS'], 'a') as destination:
            destination.write('built\n')
    elif cache.read_bytes() != struct.pack('<4I', 0, 1, 2, 3):
        sys.exit(16)
if key in scenario.get('fail', []):
    print('deliberate failure', file=sys.stderr)
    sys.exit(12)
size = scenario['sizes'][key]
perm_bytes = 16 if args.order == 'bfs-llp' else 0
base = Path(args.basename)
base.with_suffix('.graph').write_bytes(bytes(size - 35 - perm_bytes))
base.with_suffix('.offsets').write_bytes(b'abc')
base.with_suffix('.properties').write_text('nodes=4\narcs=6\n')
if perm_bytes:
    base.with_suffix('.perm').write_bytes(struct.pack('<4I', 0, 1, 2, 3))
'''

FAKE_WEBGRAPH = '''#!/usr/bin/env python3
from pathlib import Path
import sys
if sys.argv[1:] == ['--version']:
    print('fake webgraph 3')
    sys.exit(0)
assert sys.argv[1:3] == ['build', 'ef']
Path(sys.argv[3]).with_suffix('.ef').write_bytes(bytes(20))
'''


def make_case(root, sizes, fail=(), fail_before_cache=(), expected_options=None):
    ccf = root / 'g.ccf'
    write_ccf(ccf)
    encoder = root / 'wg_encode'
    webgraph = root / 'webgraph'
    encoder.write_text(FAKE_ENCODER, encoding='utf-8')
    webgraph.write_text(FAKE_WEBGRAPH, encoding='utf-8')
    os.chmod(str(encoder), 0o755)
    os.chmod(str(webgraph), 0o755)
    scenario = root / 'scenario.json'
    scenario.write_text(json.dumps({
        'sizes': sizes, 'fail': list(fail),
        'fail_before_cache': list(fail_before_cache),
        'expected_options': expected_options or {},
    }), encoding='utf-8')
    return ccf, encoder, webgraph, scenario, root / 'calls.txt', root / 'cache_builds.txt'


def run_case(case, output, deadline=None, resume=False, candidate_timeout=None):
    ccf, encoder, webgraph, scenario, calls, cache_builds = case
    with mock.patch.dict(os.environ, {
            'WG_FAKE_SCENARIO': str(scenario), 'WG_FAKE_CALLS': str(calls),
            'WG_FAKE_CACHE_BUILDS': str(cache_builds)}):
        directory = tune.tune(ccf, output, str(encoder), str(webgraph), deadline,
                              resume, candidate_timeout)
    return directory, json.loads((directory / 'manifest.json').read_text(encoding='utf-8'))


def calls_of(case):
    return case[-2].read_text(encoding='utf-8').splitlines()


class SequentialSearchTests(unittest.TestCase):
    def test_candidate_spec_can_change_interval_and_codes(self):
        setting = tune.setting_from_spec({
            'id': 'alternative', 'order': 'original', 'window': 8,
            'interval': 2, 'codes': {'blocks': 'delta'},
        })
        self.assertEqual(setting['id'], 'alternative')
        self.assertEqual(setting['min_interval_length'], 2)
        self.assertEqual(setting['blocks'], 'delta')
        self.assertEqual(setting['residuals'], tune.START_CODES['residuals'])
        with self.assertRaisesRegex(ValueError, 'Unknown candidate fields'):
            tune.setting_from_spec({'order': 'original', 'window': 8, 'max_refs': 2})
        with self.assertRaisesRegex(ValueError, 'Candidate ID'):
            tune.setting_from_spec({'id': '../bad', 'order': 'original', 'window': 8})
        with self.assertRaisesRegex(ValueError, 'max_ref_count'):
            tune.setting_from_spec({
                'order': 'original', 'window': 8, 'max_ref_count': -1,
            })
        with self.assertRaisesRegex(ValueError, 'bvgraphz'):
            tune.setting_from_spec({
                'order': 'original', 'window': 8, 'bvgraphz': 'true',
            })

    def test_candidate_can_use_bvcompz_and_finite_references(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            case = make_case(root, {'original_w16': 100}, expected_options={
                'original_w16': {'max_ref_count': 8, 'bvgraphz': True},
            })
            with mock.patch.object(tune, 'INITIAL_CANDIDATES', ({
                    'id': 'zuckerli_r8', 'order': 'original', 'window': 16,
                    'max_ref_count': 8, 'bvgraphz': True,
            },)), mock.patch.object(tune, 'EXTRA_CANDIDATES', ()):
                _, manifest = run_case(case, root / 'format_wg')
            selected = manifest['selected']
            self.assertEqual(selected['id'], 'zuckerli_r8')
            self.assertTrue(selected['bvgraphz'])
            self.assertEqual(manifest['attempts'][0]['max_ref_count'], 8)

    def test_editing_candidates_replaces_completed_search(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            case = make_case(root, {'original_w16': 100, 'original_w8': 80})
            output = root / 'format_wg'
            with mock.patch.object(tune, 'INITIAL_CANDIDATES', (
                    {'order': 'original', 'window': 16},)), \
                    mock.patch.object(tune, 'EXTRA_CANDIDATES', ()):
                _, first = run_case(case, output)
            self.assertEqual(first['selected']['id'], 'original_w16')
            with mock.patch.object(tune, 'INITIAL_CANDIDATES', (
                    {'order': 'original', 'window': 8,
                     'interval': 2, 'codes': {'blocks': 'delta'}},)), \
                    mock.patch.object(tune, 'EXTRA_CANDIDATES', ()):
                directory, second = run_case(case, output)
            self.assertEqual(second['selected']['id'], 'original_w8')
            self.assertEqual(second['selected']['runtime_bytes'], 80)
            self.assertEqual(
                {path.name for path in directory.iterdir()},
                {'g.graph', 'g.properties', 'g.ef', 'manifest.json'},
            )

    def test_search_counts_permutation_and_keeps_only_winner(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            case = make_case(root, {
                'original_w16': 120, 'original_w32': 100,
                'bfs_llp_w16': 90, 'bfs_llp_w32': 80,
                'bfs_llp_w64': 110, 'bfs_llp_w128': 70,
            })
            directory, manifest = run_case(case, root / 'format_wg')
            self.assertEqual(calls_of(case), [
                'original_w16', 'original_w32', 'bfs_llp_w16',
                'bfs_llp_w32', 'bfs_llp_w64', 'bfs_llp_w128',
            ])
            self.assertEqual(manifest['selected']['id'], 'bfs_llp_w128')
            self.assertEqual(manifest['selected']['runtime_bytes'], 70)
            self.assertEqual(manifest['selected']['files']['perm']['bytes'], 16)
            self.assertEqual(case[-1].read_text(encoding='utf-8').splitlines(), ['built'])
            self.assertEqual(
                {path.name for path in directory.iterdir()},
                {'g.graph', 'g.properties', 'g.ef', 'g.perm', 'manifest.json'},
            )
            run_case(case, root / 'format_wg')
            self.assertEqual(len(calls_of(case)), 6)

    def test_w128_requires_completed_w64(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            case = make_case(root, {
                'original_w16': 120, 'original_w32': 90,
                'bfs_llp_w16': 150, 'bfs_llp_w32': 100,
                'original_w64': 75,
            }, fail=('original_w64',))
            _, manifest = run_case(case, root / 'format_wg')
            self.assertEqual(manifest['selected']['id'], 'original_w32')
            self.assertEqual(calls_of(case)[-1], 'original_w64')
            self.assertNotIn('original_w128', calls_of(case))
            self.assertEqual(manifest['attempts'][-1]['status'], 'failed')

    def test_failed_order_computation_is_not_repeated(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            case = make_case(root, {
                'original_w16': 120, 'original_w32': 100,
                'original_w64': 95, 'original_w128': 90,
            }, fail_before_cache=('bfs_llp_w16',))
            _, manifest = run_case(case, root / 'format_wg')
            self.assertEqual(calls_of(case), [
                'original_w16', 'original_w32', 'bfs_llp_w16',
                'original_w64', 'original_w128',
            ])
            skipped = next(item for item in manifest['attempts'] if item['id'] == 'bfs_llp_w32')
            self.assertEqual(skipped['status'], 'skipped_order')

    def test_deadline_skips_extra_without_losing_best(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            case = make_case(root, {
                'original_w16': 120, 'original_w32': 100,
                'bfs_llp_w16': 90, 'bfs_llp_w32': 80,
            })
            with mock.patch.object(tune, 'remaining_timeout', side_effect=[100, 100, 100, 100, 0]):
                _, manifest = run_case(case, root / 'format_wg')
            self.assertEqual(manifest['selected']['id'], 'bfs_llp_w32')
            self.assertEqual(manifest['search_stop_reason'], 'deadline')
            self.assertEqual(manifest['attempts'][-1]['status'], 'skipped_deadline')
            self.assertEqual(len(calls_of(case)), 4)

    def test_finalized_deadline_result_resumes_without_reencoding_successes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            case = make_case(root, {
                'original_w16': 120, 'original_w32': 100,
                'bfs_llp_w16': 90, 'bfs_llp_w32': 80,
                'bfs_llp_w64': 110, 'bfs_llp_w128': 115,
            })
            output = root / 'format_wg'
            with mock.patch.object(tune, 'remaining_timeout', side_effect=[100] * 4 + [0]):
                directory, first = run_case(case, output)
            self.assertEqual(first['attempts'][-1]['status'], 'skipped_deadline')
            self.assertFalse((directory / 'state.json').exists())
            original_bundle = (directory / 'g.graph').read_bytes()

            _, resumed = run_case(case, output, resume=True, candidate_timeout=8 * 60 * 60)
            self.assertEqual(calls_of(case), [
                'original_w16', 'original_w32', 'bfs_llp_w16', 'bfs_llp_w32',
                'bfs_llp_w64', 'bfs_llp_w128',
            ])
            self.assertEqual(resumed['selected']['id'], 'bfs_llp_w32')
            self.assertEqual((directory / 'g.graph').read_bytes(), original_bundle)
            self.assertEqual(len(resumed['attempt_history']), 5)
            self.assertTrue(all(item['status'] == 'success' for item in resumed['attempts']))
            self.assertEqual(resumed['attempts'][-1]['time_limit_seconds'], 8 * 60 * 60)
            self.assertEqual(case[-1].read_text(encoding='utf-8').splitlines(), ['built'])
            self.assertEqual({path.name for path in directory.iterdir()},
                             {'g.graph', 'g.properties', 'g.ef', 'g.perm', 'manifest.json'})
            run_case(case, output, resume=True)
            self.assertEqual(len(calls_of(case)), 6)

    def test_finalized_timeout_retries_trial_and_dependent_skip(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            case = make_case(root, {
                'original_w16': 120, 'original_w32': 100,
                'bfs_llp_w16': 90, 'bfs_llp_w32': 80,
            })
            output = root / 'format_wg'
            original_run_logged = tune.run_logged
            timed_out = [False]

            def timeout_bfs_once(command, log_path, timeout=None):
                if ('--order' in command and 'bfs-llp' in command and
                        '--window' in command and '16' in command and
                        not timed_out[0]):
                    timed_out[0] = True
                    raise subprocess.TimeoutExpired(command, timeout)
                return original_run_logged(command, log_path, timeout)

            with mock.patch.object(tune, 'EXTRA_CANDIDATES', ()), \
                    mock.patch.object(tune, 'run_logged', side_effect=timeout_bfs_once):
                _, first = run_case(case, output)
            self.assertEqual([item['status'] for item in first['attempts']],
                             ['success', 'success', 'timeout', 'skipped_order'])
            with mock.patch.object(tune, 'EXTRA_CANDIDATES', ()):
                _, resumed = run_case(case, output, resume=True)
            self.assertEqual(calls_of(case), [
                'original_w16', 'original_w32', 'bfs_llp_w16', 'bfs_llp_w32',
            ])
            self.assertEqual(resumed['selected']['id'], 'bfs_llp_w32')
            self.assertTrue(all(item['status'] == 'success' for item in resumed['attempts']))
            self.assertEqual([item['status'] for item in resumed['attempt_history']],
                             ['success', 'success', 'timeout', 'skipped_order'])

    def test_resume_can_span_multiple_job_deadlines(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            case = make_case(root, {
                'original_w16': 120, 'original_w32': 100,
                'bfs_llp_w16': 90, 'bfs_llp_w32': 80,
                'bfs_llp_w64': 70, 'bfs_llp_w128': 60,
            })
            output = root / 'format_wg'
            with mock.patch.object(tune, 'remaining_timeout', side_effect=[100] * 4 + [0]):
                run_case(case, output)
            with mock.patch.object(tune, 'remaining_timeout', return_value=0):
                _, second = run_case(case, output, resume=True)
            self.assertEqual(second['attempts'][-1]['status'], 'skipped_deadline')
            _, third = run_case(case, output, resume=True)
            self.assertEqual(third['selected']['id'], 'bfs_llp_w128')
            self.assertEqual(len(third['attempt_history']), 10)
            self.assertEqual(calls_of(case), [
                'original_w16', 'original_w32', 'bfs_llp_w16', 'bfs_llp_w32',
                'bfs_llp_w64', 'bfs_llp_w128',
            ])

    def test_resumed_original_winner_removes_old_permutation(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            case = make_case(root, {
                'original_w16': 120, 'original_w32': 110,
                'bfs_llp_w16': 100, 'bfs_llp_w32': 115,
                'original_w64': 90, 'original_w128': 85,
            })
            output = root / 'format_wg'
            with mock.patch.object(tune, 'remaining_timeout',
                                   side_effect=[100] * 3 + [0, 0]):
                directory, first = run_case(case, output)
            self.assertEqual(first['selected']['id'], 'bfs_llp_w16')
            self.assertTrue((directory / 'g.perm').exists())
            _, resumed = run_case(case, output, resume=True)
            self.assertEqual(resumed['selected']['id'], 'original_w128')
            self.assertFalse((directory / 'g.perm').exists())

    def test_resume_recovers_interrupted_finalization(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            case = make_case(root, {
                'original_w16': 120, 'original_w32': 100,
                'bfs_llp_w16': 90, 'bfs_llp_w32': 80,
                'bfs_llp_w64': 70, 'bfs_llp_w128': 60,
            })
            output = root / 'format_wg'
            with mock.patch.object(tune, 'remaining_timeout', side_effect=[100] * 4 + [0]):
                run_case(case, output)
            directory = output / 'g'
            original_atomic_json = tune.atomic_json
            interrupted = [False]

            def interrupt_manifest(path, value):
                if path.name == 'manifest.json' and not interrupted[0]:
                    interrupted[0] = True
                    raise KeyboardInterrupt()
                return original_atomic_json(path, value)

            with mock.patch.object(tune, 'atomic_json', side_effect=interrupt_manifest):
                with self.assertRaises(KeyboardInterrupt):
                    run_case(case, output, resume=True)
            self.assertTrue((directory / 'state.json').exists())
            self.assertEqual(json.loads((directory / 'state.json').read_text())['phase'],
                             'finalizing')
            _, resumed = run_case(case, output, resume=True)
            self.assertEqual(resumed['selected']['id'], 'bfs_llp_w128')
            self.assertEqual(len(calls_of(case)), 6)
            self.assertFalse((directory / 'state.json').exists())

    def test_resume_recovers_before_replacing_old_manifest(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            case = make_case(root, {
                'original_w16': 120, 'original_w32': 100,
                'bfs_llp_w16': 90, 'bfs_llp_w32': 80,
                'bfs_llp_w64': 70, 'bfs_llp_w128': 60,
            })
            output = root / 'format_wg'
            with mock.patch.object(tune, 'remaining_timeout', side_effect=[100] * 4 + [0]):
                run_case(case, output)
            directory = output / 'g'
            original_save_state = tune.save_state
            interrupted = [False]

            def interrupt_after_finalizing(directory, state):
                original_save_state(directory, state)
                if state['phase'] == 'finalizing' and not interrupted[0]:
                    interrupted[0] = True
                    raise KeyboardInterrupt()

            with mock.patch.object(tune, 'save_state', side_effect=interrupt_after_finalizing):
                with self.assertRaises(KeyboardInterrupt):
                    run_case(case, output, resume=True)
            self.assertEqual(json.loads((directory / 'manifest.json').read_text())
                             ['selected']['id'], 'bfs_llp_w32')
            _, resumed = run_case(case, output, resume=True)
            self.assertEqual(resumed['selected']['id'], 'bfs_llp_w128')
            self.assertEqual(len(calls_of(case)), 6)

    def test_resume_keeps_prior_failed_trial_terminal(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            case = make_case(root, {
                'original_w16': 120, 'original_w32': 100,
                'bfs_llp_w16': 90, 'bfs_llp_w32': 80,
                'bfs_llp_w64': 70, 'bfs_llp_w128': 60,
            }, fail=('original_w16',))
            output = root / 'format_wg'
            with mock.patch.object(tune, 'remaining_timeout', side_effect=[100] * 4 + [0]):
                run_case(case, output)
            _, resumed = run_case(case, output, resume=True)
            self.assertEqual(calls_of(case).count('original_w16'), 1)
            self.assertEqual(resumed['attempts'][0]['status'], 'failed')
            self.assertFalse(any(item['status'] in ('timeout', 'skipped_deadline')
                                 for item in resumed['attempts']))

    def test_interrupted_run_resumes_after_saved_best(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            case = make_case(root, {
                'original_w16': 120, 'original_w32': 100,
                'bfs_llp_w16': 90, 'bfs_llp_w32': 80,
                'bfs_llp_w64': 70, 'bfs_llp_w128': 60,
            })
            original_run_logged = tune.run_logged

            def interrupt_bfs(command, log_path, timeout=None):
                if '--order' in command and 'bfs-llp' in command:
                    raise KeyboardInterrupt()
                return original_run_logged(command, log_path, timeout)

            with mock.patch.object(tune, 'run_logged', side_effect=interrupt_bfs):
                with self.assertRaises(KeyboardInterrupt):
                    run_case(case, root / 'format_wg')
            directory = root / 'format_wg' / 'g'
            self.assertTrue((directory / 'best' / 'g.graph').is_file())
            self.assertTrue((directory / 'state.json').is_file())
            self.assertTrue((directory / 'current').is_dir())
            _, manifest = run_case(case, root / 'format_wg')
            self.assertEqual(manifest['selected']['id'], 'bfs_llp_w128')
            self.assertEqual(calls_of(case)[:2], ['original_w16', 'original_w32'])
            self.assertEqual(calls_of(case).count('original_w16'), 1)
            self.assertEqual(calls_of(case).count('original_w32'), 1)

    def test_interrupted_best_swap_restores_saved_bundle(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            case = make_case(root, {
                'original_w16': 120, 'original_w32': 100,
                'bfs_llp_w16': 90, 'bfs_llp_w32': 80,
                'bfs_llp_w64': 70, 'bfs_llp_w128': 60,
            })
            original_save = tune.save_state

            def interrupt_swap(directory, state):
                if state['selected_id'] == 'original_w32' and state['phase'] == 'running':
                    raise KeyboardInterrupt()
                return original_save(directory, state)

            with mock.patch.object(tune, 'save_state', side_effect=interrupt_swap):
                with self.assertRaises(KeyboardInterrupt):
                    run_case(case, root / 'format_wg')
            directory = root / 'format_wg' / 'g'
            self.assertTrue((directory / 'best' / 'g.graph').is_file())
            self.assertTrue((directory / '.best_previous' / 'g.graph').is_file())
            _, manifest = run_case(case, root / 'format_wg')
            self.assertEqual(manifest['selected']['id'], 'bfs_llp_w128')
            self.assertEqual(calls_of(case).count('original_w16'), 1)
            self.assertEqual(calls_of(case).count('original_w32'), 2)

    def test_existing_completed_result_survives_failed_upgrade(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            keys = ('original_w16', 'original_w32', 'bfs_llp_w16', 'bfs_llp_w32')
            case = make_case(root, {key: 100 for key in keys}, fail=keys)
            output = root / 'format_wg'
            directory = output / 'g'
            directory.mkdir(parents=True)
            for extension, content in (
                    ('graph', b'old graph'), ('properties', b'nodes=4\narcs=6\n'),
                    ('ef', b'old ef')):
                (directory / ('g.' + extension)).write_bytes(content)
            files = {}
            for extension in tune.RUNTIME_EXTENSIONS:
                path = directory / ('g.' + extension)
                files[extension] = {
                    'name': path.name, 'bytes': path.stat().st_size,
                    'sha256': tune.sha256_file(path),
                }
            old_manifest = {
                'schema_version': 1, 'search_version': 2,
                'inputs': {'ccf': tune.read_header(case[0])},
                'selected': {'files': files},
            }
            (directory / 'manifest.json').write_text(json.dumps(old_manifest), encoding='utf-8')
            with self.assertRaisesRegex(RuntimeError, 'No WebGraph candidate completed'):
                run_case(case, output)
            self.assertEqual((directory / 'g.graph').read_bytes(), b'old graph')
            self.assertEqual(
                {path.name for path in directory.iterdir()},
                {'g.graph', 'g.properties', 'g.ef', 'manifest.json'},
            )

    def test_all_failed_without_previous_can_retry(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            keys = ('original_w16', 'original_w32', 'bfs_llp_w16', 'bfs_llp_w32')
            case = make_case(root, {key: 100 for key in keys}, fail=keys)
            output = root / 'format_wg'
            with self.assertRaisesRegex(RuntimeError, 'No WebGraph candidate completed'):
                run_case(case, output)
            self.assertFalse((output / 'g').exists())
            case[3].write_text(json.dumps({
                'sizes': {key: 100 for key in keys + ('bfs_llp_w64', 'bfs_llp_w128')},
                'fail': [],
                'fail_before_cache': [],
            }), encoding='utf-8')
            _, manifest = run_case(case, output)
            self.assertEqual(manifest['selected']['runtime_bytes'], 100)


if __name__ == '__main__':
    unittest.main()
