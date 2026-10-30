"""Modeled runner contracts, never substitutes for assembled GUI acceptance."""
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest

RUNNER = Path(__file__).with_name('run.py')


class RunnerContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.api = None
        if RUNNER.exists():
            spec = importlib.util.spec_from_file_location('acceptance119_runner_test', RUNNER)
            cls.api = importlib.util.module_from_spec(spec)
            sys.modules[spec.name] = cls.api
            spec.loader.exec_module(cls.api)

    def setUp(self):
        self.assertIsNotNone(self.api, 'The thin acceptance runner has not been implemented')
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.addCleanup(self.tmp.cleanup)

    def manifest(self):
        return {'schema': 'colosseum.acceptance119.manifest.v1', 'source_sha': 'a' * 40,
                'version': '1.1.9', 'cases': [
                    {'id': 'cold', 'scenario': 'cold.json', 'layer': 'assembled-gui',
                     'platforms': ['linux', 'windows'], 'profile_group': 'reading'},
                    {'id': 'resume', 'scenario': 'resume.json', 'layer': 'assembled-gui',
                     'platforms': ['linux', 'windows'], 'phase': 'restart', 'profile_group': 'reading'},
                    {'id': 'windows-install', 'layer': 'windows-receiving', 'platforms': ['windows'],
                     'blocked_reason': 'Receiving installer is unavailable'}]}

    def test_manifest_preserves_blocked_cases_without_scenarios(self):
        m = self.manifest()
        self.api.validate_manifest(m)
        self.assertEqual(len(self.api.select_cases(m, [], 'all')), 3)

    def test_manifest_rejects_empty_selection_duplicates_and_unknown_case(self):
        m = self.manifest()
        with self.assertRaises(ValueError):
            self.api.select_cases(m, ['typo'], 'all')
        m['cases'].append(dict(m['cases'][0]))
        with self.assertRaises(ValueError):
            self.api.validate_manifest(m)
        m['cases'] = []
        with self.assertRaises(ValueError):
            self.api.validate_manifest(m)

    def test_materialize_substitutes_strings_without_changing_types(self):
        value = {'steps': [{'value': False, 'count': 3, 'path': '${FIXTURE_ROOT}/a.cbz'}]}
        got = self.api.materialize(value, {'FIXTURE_ROOT': '/owned/f', 'OUTPUT_ROOT': '/owned/o'})
        self.assertIs(got['steps'][0]['value'], False)
        self.assertEqual(got['steps'][0]['count'], 3)
        self.assertEqual(got['steps'][0]['path'], '/owned/f/a.cbz')
        with self.assertRaises(ValueError):
            self.api.materialize('${UNKNOWN}', {})

    def test_scenario_rejects_unasserted_command_and_unknown_command(self):
        for step in ({'cmd': 'ui-click', 'payload': {'target': 'x'}},
                     {'cmd': 'open-any-file', 'expect': [{'path': 'type', 'op': '==', 'value': 'reply'}]}):
            with self.subTest(step=step), self.assertRaises(ValueError):
                self.api.validate_scenario({'steps': [step]})

    def test_scenario_preserves_typed_boolean_wait(self):
        step = {'label': 'typed', 'cmd': 'ui-wait-for',
                'payload': {'object': 'x', 'prop': 'visible', 'value': False, 'timeout_ms': 3000},
                'expect': [{'path': 'type', 'op': '==', 'value': 'reply'},
                           {'path': 'matched', 'op': '==', 'value': True}]}
        self.api.validate_scenario({'steps': [step]})
        self.assertIs(self.api.prepare_scenario({'steps': [step]})['steps'][2]['payload']['value'], False)

    def test_prepared_scenario_preserves_final_evidence_before_quit(self):
        s = self.api.prepare_scenario({'steps': [{'label': 'original', 'cmd': 'ping',
            'expect': [{'path': 'type', 'op': '==', 'value': 'reply'}]}]})
        self.assertEqual(s['steps'][0]['cmd'], 'ping')
        self.assertEqual(s['steps'][1]['cmd'], 'get-state')
        self.assertEqual(s['steps'][-3]['cmd'], 'get-state')
        self.assertIn('grab', s['steps'][-3]['payload'])
        self.assertEqual(s['steps'][-2]['payload'], {'key': 'Ctrl+Q'})
        self.assertEqual(s['steps'][-1]['presentation_wait_ms'], 15000)

    def test_shutdown_grace_is_configurable_only_within_native_bounded_range(self):
        scenario = {'steps': [{'label': 'read', 'cmd': 'ping', 'expect': self.api.reply_expect()}]}
        for grace in (500, 15000, 20000):
            with self.subTest(grace=grace):
                prepared = self.api.prepare_scenario(scenario, shutdown_grace_ms=grace)
                self.assertEqual(prepared['steps'][-1], {
                    'label': 'acceptance-shutdown-drain', 'presentation_wait_ms': grace})
                self.assertEqual(prepared['steps'][-2]['payload'], {'key': 'Ctrl+Q'})
        for grace in (0, 499, 20001, True, 15000.5):
            with self.subTest(grace=grace), self.assertRaises(ValueError):
                self.api.prepare_scenario(scenario, shutdown_grace_ms=grace)

    def test_shutdown_wait_requires_final_separate_quit_and_replaces_legacy_drain(self):
        read = {'label': 'read', 'cmd': 'ping', 'expect': self.api.reply_expect()}
        quit_step = {'label': 'old quit', 'cmd': 'ui-keypress', 'payload': {'key': 'Ctrl+Q'},
                     'expect': self.api.reply_expect()}
        wait = {'label': 'old drain', 'presentation_wait_ms': 500}
        prepared = self.api.prepare_scenario({'steps': [read, quit_step, wait]})
        self.assertEqual(prepared['steps'][-1]['presentation_wait_ms'], 15000)
        self.assertEqual(sum(s.get('payload', {}).get('key') == 'Ctrl+Q' for s in prepared['steps']), 1)
        for steps in ([read, wait], [quit_step, wait, read]):
            with self.subTest(steps=steps), self.assertRaises(ValueError):
                self.api.validate_scenario({'steps': steps})

    def test_player_fade_settle_requires_prior_readiness_and_following_picture(self):
        ready = {'label': 'ready', 'cmd': 'qml-get', 'payload': {'object': 'player'},
                 'expect': self.api.reply_expect() + [
                     {'path': 'props.playbackStarted', 'op': '==', 'value': 'true'},
                     {'path': 'props.starting', 'op': '==', 'value': 'false'},
                     {'path': 'props.playbackPosition', 'op': '>', 'value': '0.25'},
                     {'path': 'props.statusMsg', 'op': '==', 'value': ''},
                     {'path': 'props.errored', 'op': '==', 'value': 'false'}]}
        fade = {'label': 'fade', 'presentation_wait_ms': 350,
                'animation_settle': 'player-loading-opacity-300ms'}
        picture = {'label': 'picture', 'cmd': 'get-state',
                   'payload': {'grab': {'target': 'player'}}, 'expect': self.api.reply_expect()}
        self.api.prepare_scenario({'steps': [ready, fade, picture]})
        for bad in (dict(fade, presentation_wait_ms=501), dict(fade, presentation_wait_ms=True),
                    dict(fade, presentation_wait_ms=0), dict(fade, animation_settle='arbitrary'),
                    {'label': 'fade', 'presentation_wait_ms': 350}):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                self.api.validate_scenario({'steps': [ready, bad, picture]})
        for index in range(1, len(ready['expect'])):
            weak = dict(ready, expect=ready['expect'][:index] + ready['expect'][index+1:])
            with self.subTest(missing=index), self.assertRaises(ValueError):
                self.api.validate_scenario({'steps': [weak, fade, picture]})
        for steps in ([fade, picture], [ready, fade],
                      [ready, fade, dict(picture, payload={'grab': {'target': 'window'}})]):
            with self.subTest(steps=steps), self.assertRaises(ValueError):
                self.api.validate_scenario({'steps': steps})

    def test_outer_deadline_includes_declared_shutdown_grace(self):
        scenario = {'steps': [{'label': 'read', 'cmd': 'ping', 'expect': self.api.reply_expect()}]}
        fast = self.api.prepare_scenario(scenario, shutdown_grace_ms=500)
        normal = self.api.prepare_scenario(scenario, shutdown_grace_ms=15000)
        self.assertEqual(self.api.scenario_timeout_seconds(normal, 60000)
                         - self.api.scenario_timeout_seconds(fast, 60000), 14.5)

    def test_shutdown_grace_cli_default_and_override(self):
        required = ['--exe', 'app', '--qml', 'Main.qml']
        self.assertEqual(self.api.parser().parse_args(required).shutdown_grace_ms, 15000)
        self.assertEqual(self.api.parser().parse_args(required + ['--shutdown-grace-ms', '500']).shutdown_grace_ms, 500)

    def test_qml_fingerprint_detects_modified_file_and_uses_sorted_paths(self):
        qml = self.root / 'qml'; qml.mkdir()
        (qml / 'b.qml').write_text('b', encoding='utf-8')
        (qml / 'a.qml').write_text('a', encoding='utf-8')
        import hashlib
        expected = hashlib.sha256(('a.qml\n' + hashlib.sha256(b'a').hexdigest() + '\n' +
                                   'b.qml\n' + hashlib.sha256(b'b').hexdigest() + '\n').encode()).hexdigest()
        self.assertEqual(self.api.qml_fingerprint(qml), expected)
        (qml / 'a.qml').write_text('changed', encoding='utf-8')
        self.assertNotEqual(self.api.qml_fingerprint(qml), expected)

    def test_mixed_presentation_and_command_step_is_rejected(self):
        # Native dispatch chooses presentation_wait_ms BEFORE cmd. Without this
        # rejection a supposed Ctrl+Q can be skipped and SIGTERM mislabeled graceful.
        bad = {'steps': [{'label': 'quit', 'cmd': 'ui-keypress',
                         'payload': {'key': 'Ctrl+Q'}, 'presentation_wait_ms': 500,
                         'expect': [{'path': 'type', 'op': '==', 'value': 'reply'}]}]}
        with self.assertRaises(ValueError):
            self.api.validate_scenario(bad)

    def test_build_attestation_must_match_observed_binary_and_qml(self):
        identity = {'source_sha': 'a' * 40, 'version': '1.1.9',
                    'exe_sha256': 'b' * 64, 'qml_tree_sha256': 'c' * 64}
        self.api.attest_identity(identity, identity, self.manifest())
        for key in ('version', 'exe_sha256', 'qml_tree_sha256'):
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.api.attest_identity(identity, dict(identity, **{key: 'wrong'}), self.manifest())

    def test_seed_tokens_are_replaced_in_owned_copy_only(self):
        import json
        source = self.root / 'seed'; source.mkdir()
        original = {'path': '${FIXTURE_ROOT}/film.mp4', 'enabled': False}
        (source / 'installed.json').write_text(json.dumps(original))
        destination = self.root / 'copy'
        self.api.copy_seed(source, destination, {'FIXTURE_ROOT': '/owned'})
        self.assertEqual(self.api.read_json(source / 'installed.json'), original)
        self.assertEqual(self.api.read_json(destination / 'installed.json'),
                         {'path': '/owned/film.mp4', 'enabled': False})

    @unittest.skipUnless(sys.platform.startswith('linux'), 'Linux process-group ownership contract')
    def test_timeout_recovers_only_own_process_group_and_records_failure(self):
        import os
        result = self.api.execute_native([sys.executable, '-c', 'import time; time.sleep(30)'],
                                        self.root, dict(os.environ), 0.15,
                                        self.root / 'stdout', self.root / 'stderr')
        self.assertEqual(result['returncode'], 124)
        self.assertTrue(result['timeout'])
        self.assertTrue(result['recovery_actions'])
        self.assertNotEqual(result['launcher_pid'], os.getpid())

    def test_preflight_blocker_records_every_selected_case(self):
        import json
        manifest = self.root / 'manifest.json'; manifest.write_text(json.dumps(self.manifest()))
        output = self.root / 'out'
        rc = self.api.main(['--manifest', str(manifest), '--package-root', str(self.root),
                            '--lanista', str(self.root / 'absent-client'), '--exe', str(self.root / 'absent-app'),
                            '--qml', str(self.root / 'qml' / 'Main.qml'), '--output-root', str(output),
                            '--fixture-root', str(self.root / 'fixtures'), '--identity', str(self.root / 'identity.json'),
                            '--full'])
        self.assertEqual(rc, 4)
        result_path = output / 'run.json'
        self.assertTrue(result_path.exists(), 'A preflight blocker must leave per-case statuses')
        report = self.api.read_json(result_path)
        self.assertEqual(len(report['cases']), 3)
        self.assertTrue(all(row['status'] in ('BLOCKED', 'NOT RUN') for row in report['cases']))
        self.assertFalse(report['summary']['qualified'])

    def test_expected_failure_requires_exact_assertion_and_normal_other_steps(self):
        expected = {'exit_code': 1, 'label': 'wrong identity'}
        replies = [{'label': 'wrong identity', 'response': {'type': 'reply', 'props': {'id': 'actual'}}}]
        steps = [{'label': 'earlier'}, {'label': 'wrong identity'}, {'label': 'quit'}]
        good = 'PASS  earlier\nFAIL  wrong identity  [props.id == wrong — got actual]\nPASS  quit\n'
        self.assertTrue(hasattr(self.api, 'expected_failure_verdict'), 'Exact negative-oracle adjudication is not implemented')
        self.assertEqual(self.api.expected_failure_verdict(expected, 1, good, replies, steps), [])
        for code, text, data in ((0, good, replies), (1, good.replace('PASS  earlier', 'FAIL  earlier'), replies),
                                 (1, good, [{'label': 'wrong identity', 'response': {'type': 'error'}}])):
            with self.subTest(code=code, text=text, data=data):
                self.assertTrue(self.api.expected_failure_verdict(expected, code, text, data, steps))

    def test_unreviewed_mutation_name_is_rejected(self):
        m = self.manifest(); m['cases'][0]['mutation_before'] = 'delete-everything'
        with self.assertRaises(ValueError):
            self.api.validate_manifest(m)

    @unittest.skipUnless(sys.platform.startswith('linux'), 'Executable script model uses POSIX shebang')
    def test_modeled_cli_roundtrip_preserves_protocol_and_all_case_statuses(self):
        import json
        app = self.root / 'app'
        app.write_text('#!/usr/bin/env python3\nprint("Colosseum 1.1.9")\n')
        app.chmod(0o755)
        qml = self.root / 'qml'; qml.mkdir(); (qml / 'Main.qml').write_text('import QtQuick\nItem {}\n')
        fixture = self.root / 'fixtures'; fixture.mkdir()
        self.make_owned_fixture(fixture)
        identity = self.api.observe_identity(app, qml / 'Main.qml')
        identity['source_sha'] = 'a6104f7a8d9aab9766e71f031b7908beaeca85ad'
        identity_path = self.root / 'identity.json'; identity_path.write_text(json.dumps(identity))
        scenario = self.root / 'cold.json'
        scenario.write_text(json.dumps({'steps': [{'label': 'original ping', 'cmd': 'ping',
            'expect': [{'path': 'type', 'op': '==', 'value': 'reply'}]}]}))
        m = self.manifest(); m['source_sha'] = identity['source_sha']; m['cases'] = m['cases'][:1]
        manifest = self.root / 'manifest.json'; manifest.write_text(json.dumps(m))
        client = self.root / 'lanista-model'
        client.write_text(r'''#!/usr/bin/env python3
import sys, os, json, hashlib, pathlib, base64
args=sys.argv
value=lambda k: args[args.index(k)+1]
tag=value('--tag'); exe=value('--exe'); sid='model-'+tag
appdata=pathlib.Path(os.environ['XDG_DATA_HOME'])/('Colosseum-dltest-'+tag)
cache=pathlib.Path(os.environ['XDG_CACHE_HOME'])/('Colosseum-dltest-'+tag)
run=appdata/'lanista'/'runs'/sid; run.mkdir(parents=True)
cache.mkdir(parents=True)
shot=run/'model.png'
shot.write_bytes(base64.b64decode('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAIAAACQd1PeAAAADElEQVR4nGP4//8/AAX+Av4N70a4AAAAAElFTkSuQmCC'))
pipe='ColosseumLanista-'+sid
session={'sessionId':sid,'pid':os.getpid(),'tag':tag,'pipe':pipe,'exe':exe,
'exeSha256':hashlib.sha256(pathlib.Path(exe).read_bytes()).hexdigest(),
'appDataRoot':str(appdata),'cacheRoot':str(cache),'exitCode':0,'crashed':False,'killReason':'graceful'}
folder=pathlib.Path('artifacts/lanista-sessions')/sid; folder.mkdir(parents=True)
(folder/'session.json').write_text(json.dumps(session))
steps=json.loads(pathlib.Path(args[3]).read_text())['steps']
(folder/'timings.json').write_text(json.dumps({'schema':'colosseum.lanista.timings.v1',
'sessionId':sid,'milestones':[],'steps':[{'label':s['label'],'pass':True,'durationMs':1} for s in steps]}))
for step in steps:
 if 'cmd' in step:
  reply={'type':'reply'}
  if step['cmd']=='ping': reply.update(schema='colosseum.dev.v1.0',pid=os.getpid(),pipe=pipe,gates={'drive':True,'write':False})
  if step['cmd']=='get-state': reply.update(appDataRoot=str(appdata),cacheRoot=str(cache),runDir=str(run))
  if step.get('payload',{}).get('grab'): reply['grabPath']=str(shot)
  if step['cmd']=='ui-keypress': reply['pressed']=step['payload']['key']
  print('  reply '+step['label']+': '+json.dumps(reply))
 print('PASS  '+step['label'])
''')
        client.chmod(0o755)
        output = self.root / 'out'
        rc = self.api.main(['--manifest', str(manifest), '--package-root', str(self.root),
            '--source-root', str(self.root), '--lanista', str(client), '--exe', str(app),
            '--qml', str(qml / 'Main.qml'), '--output-root', str(output),
            '--fixture-root', str(fixture), '--identity', str(identity_path), '--full',
            '--shutdown-grace-ms', '17000'])
        self.assertEqual(rc, 0)
        report = self.api.read_json(output / 'run.json')
        self.assertEqual(report['cases'][0]['status'], 'PASS')
        self.assertEqual(report['cases'][0]['execution']['returncode'], 0)
        self.assertTrue(report['profiles']['reading']['completed_fresh'])
        self.assertTrue(Path(report['cases'][0]['protocol']).is_file())
        self.assertTrue(report['cases'][0]['evidence'])
        self.assertEqual(report['shutdown_grace_ms'], 17000)
        self.assertEqual(report['cases'][0]['shutdown_grace_ms'], 17000)
        prepared = self.api.read_json(report['cases'][0]['scenario'])
        self.assertEqual(prepared['steps'][-1]['presentation_wait_ms'], 17000)

    @unittest.skipUnless(sys.platform.startswith('linux'), 'Linux helper shutdown contract')
    def test_normal_owned_helpers_receive_bounded_shutdown_grace(self):
        import os
        child = 'import time; time.sleep(0.35)'
        parent = 'import subprocess,sys; subprocess.Popen([sys.executable,"-c",' + repr(child) + '])'
        result = self.api.execute_native([sys.executable, '-c', parent], self.root, dict(os.environ), 3,
                                        self.root / 'stdout', self.root / 'stderr')
        self.assertEqual(result['returncode'], 0)
        self.assertEqual(result['recovery_actions'], [])
        self.assertEqual(result['remaining_live_processes'], {})

    def make_owned_fixture(self, root):
        import json
        root.mkdir(exist_ok=True)
        source_sha = 'a6104f7a8d9aab9766e71f031b7908beaeca85ad'
        owner = {'schema': 'colosseum.acceptance119.fixtures.v1', 'source_sha': source_sha,
                 'root': root.absolute().as_posix()}
        (root / '.acceptance-119-fixtures.json').write_text(json.dumps(owner))
        seed = root / 'seeds' / 'one'; seed.mkdir(parents=True, exist_ok=True)
        (seed / 'config.json').write_text(json.dumps({'path': (root / 'book.pdf').as_posix()}))
        manifest = dict(owner, tokens={'FIXTURE_ROOT': root.as_posix(),
                        'VAULT_EMPTY_ROOT': (root / 'empty').as_posix()},
                        seeds={'one': seed.as_posix()}, files={})
        (root / 'fixture-manifest.json').write_text(json.dumps(manifest))
        return manifest

    def test_fixture_manifest_rejects_stale_root_source_schema_and_seed_escape(self):
        import json
        root = self.root / 'fixture'; manifest = self.make_owned_fixture(root)
        self.assertTrue(hasattr(self.api, 'validate_fixture_manifest'), 'Fixture binding preflight is missing')
        self.api.validate_fixture_manifest(root, manifest['source_sha'])
        variants = [dict(manifest, schema='wrong'), dict(manifest, source_sha='f' * 40),
                    dict(manifest, tokens=dict(manifest['tokens'], FIXTURE_ROOT=str(self.root / 'old'))),
                    dict(manifest, tokens=dict(manifest['tokens'], VAULT_EMPTY_ROOT=str(self.root / 'old'))),
                    dict(manifest, seeds={'one': str(self.root / 'old')})]
        for variant in variants:
            with self.subTest(variant=variant):
                (root / 'fixture-manifest.json').write_text(json.dumps(variant))
                with self.assertRaises(ValueError):
                    self.api.validate_fixture_manifest(root, manifest['source_sha'])
        (root / 'fixture-manifest.json').write_text(json.dumps(manifest))
        (root / 'seeds/one/config.json').write_text(json.dumps({'path': str(self.root / 'old/personal.cbz')}))
        with self.assertRaises(ValueError):
            self.api.validate_fixture_manifest(root, manifest['source_sha'])

    @unittest.skipUnless(hasattr(Path, 'symlink_to'), 'Symlink contract')
    def test_fixture_manifest_rejects_symlink_component_before_resolution(self):
        import json
        root = self.root / 'fixture'; manifest = self.make_owned_fixture(root)
        self.assertTrue(hasattr(self.api, 'validate_fixture_manifest'), 'Fixture binding preflight is missing')
        outside = self.root / 'outside'; outside.mkdir()
        (root / 'escape').symlink_to(outside, target_is_directory=True)
        manifest['tokens']['VAULT_EMPTY_ROOT'] = str(root / 'escape/library')
        (root / 'fixture-manifest.json').write_text(json.dumps(manifest))
        with self.assertRaises(ValueError):
            self.api.validate_fixture_manifest(root, manifest['source_sha'])

    def test_check_png_rejects_wrong_rows_bad_crc_and_short_ihdr(self):
        import base64, struct, zlib
        def chunk(kind, data):
            return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)
        header = b'\x89PNG\r\n\x1a\n'
        malformed = [header + chunk(b'IHDR', struct.pack('>IIBBBBB', 480, 640, 8, 2, 0, 0, 0))
                     + chunk(b'IDAT', zlib.compress(b'not pixels')) + chunk(b'IEND', b''),
                     header + chunk(b'IHDR', b'xx') + chunk(b'IEND', b''),
                     base64.b64decode('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+/l9sAAAAASUVORK5CYII=')]
        for index, data in enumerate(malformed):
            path = self.root / ('malformed' + str(index) + '.png'); path.write_bytes(data)
            with self.subTest(index=index), self.assertRaises(ValueError):
                self.api.check_png(path)

    def test_restart_rejects_changed_qml_and_exact_root_drift(self):
        prior = {'schema': 'colosseum.acceptance119.run.v1', 'ownership_token': 'proof',
                 'source_sha': 'a' * 40,
                 'identity': {'exe_sha256': 'b' * 64, 'qml_tree_sha256': 'c' * 64},
                 'profiles': {'reading': {'tag': '119-owned', 'completed_fresh': True}}}
        with self.assertRaises(ValueError):
            self.api.restart_profile(prior, 'reading', 'a' * 40, 'b' * 64, 'd' * 64)
        self.assertTrue(hasattr(self.api, 'validate_restart_roots'), 'Exact restart root comparison is missing')
        profile = {'appDataRoot': '/owned/a/Colosseum-dltest-119-owned',
                   'cacheRoot': '/owned/b/Colosseum-dltest-119-owned'}
        self.api.validate_restart_roots(profile, profile)
        with self.assertRaises(ValueError):
            self.api.validate_restart_roots(profile, dict(profile,
                appDataRoot='/different/Colosseum-dltest-119-owned'))

    def test_restart_exact_roots_are_guarded_before_journey_actions(self):
        s = self.api.prepare_scenario({'steps': [{'label': 'first action', 'cmd': 'ui-click',
            'payload': {'target': 'safe'}, 'expect': [{'path': 'type', 'op': '==', 'value': 'reply'}]}]})
        profile = {'appDataRoot': '/owned/a', 'cacheRoot': '/owned/b'}
        self.assertTrue(hasattr(self.api, 'bind_restart_root_guards'), 'Restart root pre-action guard is missing')
        self.api.bind_restart_root_guards(s, profile)
        self.assertEqual(s['steps'][1]['cmd'], 'get-state')
        self.assertEqual(s['steps'][1]['expect'][-2:], [
            {'path': 'appDataRoot', 'op': '==', 'value': '/owned/a'},
            {'path': 'cacheRoot', 'op': '==', 'value': '/owned/b'}])
        self.assertEqual(s['steps'][2]['cmd'], 'ui-click')

    def session(self):
        tag = '119-owned'
        data = self.root / 'data' / ('Colosseum-dltest-' + tag); data.mkdir(parents=True)
        cache = self.root / 'cache' / ('Colosseum-dltest-' + tag); cache.mkdir(parents=True)
        exe = self.root / 'app'; exe.write_bytes(b'candidate')
        session = {'pid': 456, 'tag': tag, 'pipe': 'ColosseumLanista-unique',
                   'appDataRoot': str(data), 'cacheRoot': str(cache), 'exe': str(exe),
                   'exeSha256': self.api.sha256(exe), 'exitCode': 0, 'crashed': False,
                   'killReason': 'graceful'}
        replies = [{'label': 'acceptance-owned-ping', 'response': {'type': 'reply',
                    'schema': 'colosseum.dev.v1.0', 'pid': 456, 'pipe': session['pipe'],
                    'gates': {'drive': True, 'write': False}}},
                   {'label': 'acceptance-owned-state', 'response': {'type': 'reply',
                    'appDataRoot': str(data), 'cacheRoot': str(cache)}},
                   {'label': 'acceptance-normal-quit', 'response': {'type': 'reply', 'pressed': 'Ctrl+Q'}}]
        return session, replies, tag, exe

    def test_zero_client_exit_does_not_hide_crash_forced_kill_or_wrong_pid(self):
        session, replies, tag, exe = self.session()
        self.assertEqual(self.api.adjudicate_session(0, session, replies, tag, exe, self.api.sha256(exe)), [])
        for mutation in ({'exitCode': 15}, {'exitCode': 11, 'crashed': True}, {'crashed': True},
                         {'killReason': 'killed after graceful timeout'},
                         {'pid': 999}, {'pipe': 'ColosseumLanista'}, {'exeSha256': 'wrong'}):
            with self.subTest(mutation=mutation):
                bad = dict(session, **mutation)
                self.assertTrue(self.api.adjudicate_session(0, bad, replies, tag, exe, self.api.sha256(exe)))

    def test_graceful_label_without_actual_quit_reply_is_failure(self):
        s, replies, tag, exe = self.session()
        self.assertTrue(self.api.adjudicate_session(0, s, replies[:-1], tag, exe, self.api.sha256(exe)))

    def test_wrong_profile_root_is_failure(self):
        s, replies, tag, exe = self.session()
        s['appDataRoot'] = str(self.root / 'Colosseum')
        self.assertTrue(self.api.adjudicate_session(0, s, replies, tag, exe, self.api.sha256(exe)))

    def test_evidence_recovery_uses_reported_owned_run_after_quit(self):
        s, replies, tag, exe = self.session()
        run_dir = Path(s['appDataRoot']) / 'lanista' / 'runs' / 'one'; run_dir.mkdir(parents=True)
        import base64
        png = run_dir / 'seq101-1.png'
        png.write_bytes(base64.b64decode('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAIAAACQd1PeAAAADElEQVR4nGP4//8/AAX+Av4N70a4AAAAAElFTkSuQmCC'))
        replies.append({'label': 'last-before-quit', 'response': {'type': 'reply', 'runDir': str(run_dir), 'grabPath': str(png)}})
        paths, failures = self.api.collect_app_evidence(s, replies, self.root / 'evidence')
        self.assertEqual(failures, [])
        self.assertTrue(any(Path(p).read_bytes() == png.read_bytes() for p in paths))
        self.assertTrue(png.exists())

    def test_evidence_recovery_refuses_outside_profile(self):
        s, replies, tag, exe = self.session()
        external = self.root / 'other.png'; external.write_bytes(b'private')
        replies.append({'label': 'bad', 'response': {'type': 'reply', 'grabPath': str(external)}})
        paths, failures = self.api.collect_app_evidence(s, replies, self.root / 'out')
        self.assertTrue(failures)
        self.assertEqual(paths, [])

    def test_incomplete_required_cases_never_qualify(self):
        for status in ('BLOCKED', 'NOT RUN', 'FAIL'):
            summary = self.api.summarize([{'status': 'PASS'}, {'status': status}])
            self.assertFalse(summary['qualified'])
            self.assertNotEqual(summary['exit_code'], 0)
        self.assertFalse(self.api.summarize([])['qualified'])
        self.assertTrue(self.api.summarize([{'status': 'PASS'}])['qualified'])

    def test_restart_uses_exact_owned_group_and_does_not_reseed(self):
        prior = {'schema': 'colosseum.acceptance119.run.v1', 'ownership_token': 'proof',
                 'source_sha': 'a' * 40, 'identity': {'exe_sha256': 'b' * 64, 'qml_tree_sha256': 'c' * 64},
                 'profiles': {'reading': {'tag': '119-owned', 'completed_fresh': True}}}
        self.assertEqual(self.api.restart_profile(prior, 'reading', 'a' * 40, 'b' * 64, 'c' * 64)['tag'], '119-owned')
        with self.assertRaises(ValueError):
            self.api.restart_profile(prior, 'unknown', 'a' * 40, 'b' * 64, 'c' * 64)
        with self.assertRaises(ValueError):
            self.api.restart_profile(prior, 'reading', 'c' * 40, 'b' * 64, 'c' * 64)


if __name__ == '__main__':
    unittest.main()
