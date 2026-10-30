#!/usr/bin/env python3
"""Portable acceptance orchestration over native Lanista, never a second IPC stack.

`--inspect` prints observations; it does not attest a build. Execution requires a
separately reviewed identity JSON and an explicit selected-case manifest. GUI
journeys alone cannot qualify native/component or Windows receiving coverage.
"""
from __future__ import annotations

import argparse
import copy
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import os
from pathlib import Path, PureWindowsPath
import platform
import re
import shutil
import signal
import struct
import subprocess
import sys
import time
import uuid
import warnings
from urllib.parse import unquote, urlsplit
import zlib

MANIFEST_SCHEMA = 'colosseum.acceptance119.manifest.v1'
RUN_SCHEMA = 'colosseum.acceptance119.run.v1'
COMMANDS = {'ping', 'get-state', 'qml-get', 'ui-query', 'dump-ui', 'ui-snapshot',
            'ui-wait-for', 'invoke-read', 'events-tail', 'log-mark', 'vault-forensics',
            'ui-click', 'ui-keypress', 'ui-text-input', 'ui-scroll', 'window-set-state'}
OPS = {'exists', '==', '!=', 'contains', 'matches', '>=', '<=', '>', '<'}
TOKEN = re.compile(r'\$\{([A-Z][A-Z0-9_]*)\}')
MUTATIONS = {'comic-a-missing', 'comic-a-restore', 'vault-add-clip', 'vault-remove-added-clip'}
SAFE_ID = re.compile(r'^[A-Za-z0-9][A-Za-z0-9_.-]{0,95}$')
DEFAULT_SHUTDOWN_GRACE_MS = 15000


def utc_now():
    return datetime.now(timezone.utc).isoformat()


def read_json(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def write_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + '.tmp')
    tmp.write_text(json.dumps(value, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    tmp.replace(path)


def sha256(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def qml_fingerprint(root):
    root = Path(root)
    files = sorted((p for p in root.rglob('*') if p.is_file()), key=lambda p: p.relative_to(root).as_posix())
    if not files:
        raise ValueError('QML tree is absent or empty: ' + str(root))
    material = ''.join(p.relative_to(root).as_posix() + '\n' + sha256(p) + '\n' for p in files)
    return hashlib.sha256(material.encode('utf-8')).hexdigest()


def materialize(value, tokens):
    if isinstance(value, str):
        def replace(match):
            key = match.group(1)
            if key not in tokens or not isinstance(tokens[key], (str, int, float)):
                raise ValueError('Unbound or invalid scenario token: ' + key)
            return str(tokens[key])
        return TOKEN.sub(replace, value)
    if isinstance(value, list):
        return [materialize(v, tokens) for v in value]
    if isinstance(value, dict):
        return {k: materialize(v, tokens) for k, v in value.items()}
    return value


def validate_manifest(manifest):
    if manifest.get('schema') != MANIFEST_SCHEMA:
        raise ValueError('Unsupported acceptance manifest schema')
    if not re.fullmatch(r'[0-9a-f]{40}', manifest.get('source_sha', '')):
        raise ValueError('Manifest requires an exact source_sha')
    if not isinstance(manifest.get('version'), str) or not manifest['version']:
        raise ValueError('Manifest requires an expected version')
    cases = manifest.get('cases')
    if not isinstance(cases, list) or not cases:
        raise ValueError('Manifest must select at least one case')
    seen = set()
    for case in cases:
        cid = case.get('id', '')
        if not SAFE_ID.fullmatch(cid) or cid in seen:
            raise ValueError('Invalid or duplicate case ID: ' + cid)
        seen.add(cid)
        if case.get('mutation_before') and case['mutation_before'] not in MUTATIONS:
            raise ValueError(cid + ': mutation_before is not an owned fixture mutation')
        expected = case.get('expected_failure')
        if expected and (not isinstance(expected, dict) or expected.get('exit_code') != 1
                         or not isinstance(expected.get('label'), str) or not expected['label']):
            raise ValueError(cid + ': expected_failure requires exit_code=1 and exact label')
        if case.get('expect_failure'):
            raise ValueError(cid + ': boolean expect_failure is unsafe; name the exact expected_failure label')
        if not case.get('layer') or not isinstance(case.get('platforms'), list) or not case['platforms']:
            raise ValueError(cid + ': layer and nonempty platforms are required')
        if any(p not in ('linux', 'windows', 'macos') for p in case['platforms']):
            raise ValueError(cid + ': unsupported platform')
        if case.get('phase', 'fresh') not in ('fresh', 'restart'):
            raise ValueError(cid + ': phase must be fresh or restart')
        if not case.get('scenario') and not case.get('blocked_reason'):
            raise ValueError(cid + ': scenario or explicit blocked_reason is required')
        if not SAFE_ID.fullmatch(case.get('profile_group', cid)):
            raise ValueError(cid + ': invalid profile_group')
        if case.get('phase') == 'restart' and not case.get('profile_group'):
            raise ValueError(cid + ': restart requires profile_group')
        if case.get('phase') == 'restart' and case.get('seed'):
            raise ValueError(cid + ': restart must not reseed an existing profile')


def select_cases(manifest, ids, phase):
    validate_manifest(manifest)
    known = {c['id'] for c in manifest['cases']}
    if set(ids) - known:
        raise ValueError('Unknown case IDs: ' + ', '.join(sorted(set(ids) - known)))
    cases = [c for c in manifest['cases'] if (not ids or c['id'] in ids)
             and (phase == 'all' or c.get('phase', 'fresh') == phase)]
    if not cases:
        raise ValueError('Selection contains zero cases')
    return cases


def validate_shutdown_grace(grace_ms):
    if type(grace_ms) is not int or not 500 <= grace_ms <= 20000:
        raise ValueError('shutdown-grace-ms must be an integer in [500,20000]')


def validate_scenario(scenario):
    steps = scenario.get('steps')
    if not isinstance(steps, list) or not steps:
        raise ValueError('Scenario requires nonempty steps')
    labels = set()
    for index, step in enumerate(steps):
        if not isinstance(step, dict):
            raise ValueError('Scenario step must be an object')
        label = step.get('label', step.get('cmd', 'step-' + str(index)))
        if not isinstance(label, str) or not label or label in labels or '\n' in label:
            raise ValueError('Scenario labels must be unique nonempty single lines')
        labels.add(label)
        modes = ('cmd', 'layout_verdict', 'presentation_wait_ms', 'presentation_hold_ms', 'capture', 'expect_visual')
        if sum(key in step for key in modes) != 1:
            raise ValueError(label + ': exactly one native dispatch mode is required; mixed command/presentation skips commands')
        if 'layout_verdict' in step:
            if not step['layout_verdict'].get('checkpoint') or not step['layout_verdict'].get('out'):
                raise ValueError(label + ': layout checkpoint and output required')
            continue
        if 'presentation_wait_ms' in step:
            previous = steps[index - 1] if index else {}
            if 'animation_settle' in step:
                following = steps[index + 1] if index + 1 < len(steps) else {}
                required = [
                    {'path': 'props.playbackStarted', 'op': '==', 'value': 'true'},
                    {'path': 'props.starting', 'op': '==', 'value': 'false'},
                    {'path': 'props.playbackPosition', 'op': '>', 'value': '0.25'},
                    {'path': 'props.statusMsg', 'op': '==', 'value': ''},
                    {'path': 'props.errored', 'op': '==', 'value': 'false'},
                ]
                wait_ms = step['presentation_wait_ms']
                if (step['animation_settle'] != 'player-loading-opacity-300ms'
                        or type(wait_ms) is not int or not 1 <= wait_ms <= 500
                        or previous.get('cmd') != 'qml-get'
                        or previous.get('payload', {}).get('object') != 'player'
                        or not all(check in previous.get('expect', []) for check in required)
                        or following.get('cmd') != 'get-state'
                        or following.get('payload', {}).get('grab', {}).get('target') != 'player'):
                    raise ValueError('Player animation settle requires bounded <=500 ms, prior playback readiness assertions, and following player capture')
                continue
            validate_shutdown_grace(step['presentation_wait_ms'])
            if (index != len(steps) - 1 or previous.get('cmd') != 'ui-keypress'
                    or previous.get('payload', {}).get('key') != 'Ctrl+Q'):
                raise ValueError('Shutdown drain must follow a separate final Ctrl+Q, never a readiness sleep')
            continue
        if step.get('cmd') not in COMMANDS:
            raise ValueError(label + ': unsupported command for this suite')
        expects = step.get('expect', [])
        if not any(e.get('path') == 'type' and e.get('op') == '==' and e.get('value') in ('reply', 'error')
                   for e in expects):
            raise ValueError(label + ': every command requires explicit type expectation')
        if any(e.get('op') not in OPS or not isinstance(e.get('path'), str) for e in expects):
            raise ValueError(label + ': invalid assertion operator/path')
        payload = step.get('payload', {})
        if step['cmd'] == 'ui-wait-for':
            timeout = payload.get('timeout_ms')
            if not isinstance(timeout, int) or not 1 <= timeout <= 120000:
                raise ValueError(label + ': explicit bounded timeout_ms is required')
            if not payload.get('object') or not payload.get('prop') or 'value' not in payload:
                raise ValueError(label + ': wait requires object, prop, and typed value')


def reply_expect():
    return [{'path': 'type', 'op': '==', 'value': 'reply'}]


def prepare_scenario(scenario, shutdown_grace_ms=DEFAULT_SHUTDOWN_GRACE_MS):
    validate_shutdown_grace(shutdown_grace_ms)
    scenario = copy.deepcopy(scenario)
    validate_scenario(scenario)
    steps = scenario['steps']
    # Normalize the final quit only; do not permit earlier quit then phantom passes.
    if 'presentation_wait_ms' in steps[-1]:
        steps.pop()
    quit_indexes = [i for i, s in enumerate(steps) if s.get('cmd') == 'ui-keypress'
                    and s.get('payload', {}).get('key') == 'Ctrl+Q']
    if quit_indexes and quit_indexes != [len(steps) - 1]:
        raise ValueError('Ctrl+Q is permitted only as the final command')
    if quit_indexes:
        steps.pop()
    scenario['steps'] = [
        {'label': 'acceptance-owned-ping', 'cmd': 'ping', 'expect': reply_expect() + [
            {'path': 'schema', 'op': '==', 'value': 'colosseum.dev.v1.0'},
            {'path': 'gates.drive', 'op': '==', 'value': True},
            {'path': 'gates.write', 'op': '==', 'value': False}]},
        {'label': 'acceptance-owned-state', 'cmd': 'get-state', 'expect': reply_expect()},
    ] + steps + [
        {'label': 'acceptance-final-state', 'cmd': 'get-state',
         'payload': {'grab': {'target': 'window', 'timeoutMs': 4000}},
         'expect': reply_expect() + [{'path': 'grabPath', 'op': 'exists'}]},
        {'label': 'acceptance-normal-quit', 'cmd': 'ui-keypress',
         'payload': {'key': 'Ctrl+Q'}, 'expect': reply_expect()},
        # Native stopSession terminates immediately after the scenario. Its
        # subsequent 8-second wait is recovery, not a normal-quit grace. This
        # bounded runner-local wait lets asynchronous app teardown finish first.
        {'label': 'acceptance-shutdown-drain', 'presentation_wait_ms': shutdown_grace_ms},
    ]
    validate_scenario(scenario)
    return scenario


def scenario_timeout_seconds(scenario, ready_ms):
    deadline = ready_ms / 1000 + 35
    for step in scenario['steps']:
        payload = step.get('payload', {})
        deadline += max(10, max(payload.get('timeout_ms', 0), payload.get('timeoutMs', 0)) / 1000 + 5)
        deadline += step.get('presentation_wait_ms', 0) / 1000
        if 'layout_verdict' in step:
            deadline += 60
    return deadline


def parse_replies(stdout):
    replies = []
    for line in stdout.splitlines():
        if not line.startswith('  reply '):
            continue
        # Labels may contain colons. JSON always begins after ': {'.
        label, separator, body = line[len('  reply '):].partition(': {')
        if separator:
            try:
                replies.append({'label': label, 'response': json.loads('{' + body)})
            except json.JSONDecodeError:
                raise ValueError('Malformed native Lanista verbose reply: ' + label)
    return replies


def adjudicate_session(returncode, session, replies, tag, exe, exe_hash):
    failures = []
    if returncode != 0:
        failures.append('Lanista exited with code ' + str(returncode))
    if not session:
        return failures + ['Owned session manifest is missing']
    pipe = session.get('pipe', '')
    if pipe == 'ColosseumLanista' or not pipe.startswith('ColosseumLanista-'):
        failures.append('Session did not use a unique nondefault pipe')
    if session.get('tag') != tag:
        failures.append('Session tag mismatch')
    marker = 'Colosseum-dltest-' + tag
    for key in ('appDataRoot', 'cacheRoot'):
        if marker not in Path(session.get(key, '')).parts:
            failures.append(key + ' is not the exact owned tagged root')
    if session.get('exeSha256') != exe_hash or Path(session.get('exe', '')).resolve() != Path(exe).resolve():
        failures.append('Launched executable identity mismatch')
    by_label = {r['label']: r['response'] for r in replies}
    ping = by_label.get('acceptance-owned-ping', {})
    if (ping.get('type') != 'reply' or ping.get('schema') != 'colosseum.dev.v1.0'
            or ping.get('pid') != session.get('pid') or not isinstance(ping.get('pid'), int)
            or ping.get('pid', 0) <= 0 or ping.get('pipe') != pipe
            or ping.get('gates', {}).get('drive') is not True
            or ping.get('gates', {}).get('write') is not False):
        failures.append('Missing or inconsistent owned ping exchange')
    state = by_label.get('acceptance-owned-state', {})
    if state.get('type') != 'reply' or any(state.get(k) != session.get(k) for k in ('appDataRoot', 'cacheRoot')):
        failures.append('Missing or inconsistent owned get-state exchange')
    quit_reply = by_label.get('acceptance-normal-quit', {})
    if quit_reply.get('type') != 'reply' or quit_reply.get('pressed') != 'Ctrl+Q':
        failures.append('Normal application quit was not acknowledged')
    if session.get('exitCode') != 0 or session.get('crashed') is not False:
        failures.append('Application did not exit normally with code zero')
    if session.get('killReason') != 'graceful':
        failures.append('Forced shutdown/recovery is not normal shutdown')
    return failures


def inside(path, root):
    try:
        Path(path).resolve().relative_to(Path(root).resolve())
        return True
    except (ValueError, OSError):
        return False


def require_png_decoder():
    try:
        from PIL import Image
    except ImportError as error:
        raise ValueError('Pillow is required to decode PNG evidence; install the reviewed Pillow package') from error
    return Image


def check_png(path):
    Image = require_png_decoder()
    try:
        with warnings.catch_warnings():
            warnings.simplefilter('error', Image.DecompressionBombWarning)
            with Image.open(path) as probe:
                if probe.format != 'PNG' or probe.width <= 0 or probe.height <= 0:
                    raise ValueError('Evidence must be a nonzero PNG')
                width, height = probe.size
                probe.verify()  # Checks PNG structure and chunk CRCs.
            # verify() does not decode pixels; reopen and force the actual decoder.
            with Image.open(path) as decoded:
                decoded.load()
                if decoded.size != (width, height):
                    raise ValueError('PNG dimensions changed during decode')
        return {'width': width, 'height': height, 'validation': 'Pillow verify and full pixel decode'}
    except (OSError, SyntaxError, ValueError, struct.error,
            Image.DecompressionBombError, Image.DecompressionBombWarning) as error:
        raise ValueError('Unreadable PNG evidence: ' + str(path) + ': ' + str(error)) from error


def collect_app_evidence(session, replies, output):
    paths, failures = [], []
    root_value = session.get('appDataRoot')
    if not root_value:
        return paths, ['No proven AppData root for evidence recovery']
    root = Path(root_value).resolve()
    if 'Colosseum-dltest-' + session.get('tag', '') not in root.parts:
        return paths, ['Refusing evidence from an unowned profile root']
    wanted = set()
    for row in replies:
        reply = row['response']
        if reply.get('grabPath'):
            p = Path(reply['grabPath'])
            if not inside(p, root):
                failures.append('Refused evidence path outside owned profile: ' + str(p))
            else:
                wanted.add(p)
        if reply.get('runDir'):
            directory = Path(reply['runDir'])
            if not inside(directory, root / 'lanista' / 'runs'):
                failures.append('Refused run directory outside owned Lanista runs')
            elif directory.is_dir():
                wanted.update(directory.glob('*.png'))
    for p in (root / 'colosseum.log', root / 'lanista' / 'events.jsonl'):
        if p.is_file():
            wanted.add(p)
    for source in sorted(wanted):
        try:
            if not inside(source, root) or not source.is_file():
                raise ValueError('Missing or escaped required evidence: ' + str(source))
            if source.suffix.lower() == '.png':
                check_png(source)
            destination = Path(output) / source.resolve().relative_to(root)
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, destination)
            paths.append(str(destination))
        except (OSError, ValueError, zlib.error) as error:
            failures.append(str(error))
    return paths, failures


def expected_failure_verdict(expected, returncode, stdout, replies, steps):
    failures = []
    if returncode != 1 or expected.get('exit_code') != 1:
        failures.append('Negative oracle did not return exactly native assertion exit 1')
    labels = [step.get('label', step.get('cmd')) for step in steps]
    verdicts = {}
    for line in stdout.splitlines():
        match = re.match(r'^(PASS|FAIL|INFRA)\s+(.*)$', line)
        if not match:
            continue
        for label in sorted(labels, key=len, reverse=True):
            text = match.group(2)
            if text == label or text.startswith(label + '  [') or text.startswith(label + '  evidence:'):
                verdicts[label] = match.group(1)
                break
    wanted = expected['label']
    if wanted not in labels:
        failures.append('Expected negative label is not a prepared scenario step')
    for label in labels:
        if verdicts.get(label) != ('FAIL' if label == wanted else 'PASS'):
            failures.append('Negative oracle unexpected/missing native verdict: ' + label)
    observed = next((row['response'] for row in replies if row['label'] == wanted), {})
    if observed.get('type') != 'reply':
        failures.append('Negative oracle failed transport/command instead of its value assertion')
    return failures


def summarize(results):
    counts = {s: sum(r.get('status') == s for r in results) for s in ('PASS', 'FAIL', 'BLOCKED', 'NOT RUN')}
    qualified = bool(results) and counts['PASS'] == len(results)
    return {'counts': counts, 'qualified': qualified,
            'exit_code': 0 if qualified else (1 if counts['FAIL'] else 4)}


def restart_profile(prior, group, source_sha, exe_sha256, qml_tree_sha256):
    if (prior.get('schema') != RUN_SCHEMA or not prior.get('ownership_token')
            or prior.get('source_sha') != source_sha
            or prior.get('identity', {}).get('exe_sha256') != exe_sha256
            or not qml_tree_sha256
            or prior.get('identity', {}).get('qml_tree_sha256') != qml_tree_sha256):
        raise ValueError('Restart requires matching prior owned run and candidate identity')
    profile = prior.get('profiles', {}).get(group)
    if not profile or not profile.get('completed_fresh') or not SAFE_ID.fullmatch(profile.get('tag', '')):
        raise ValueError('No completed owned fresh profile for restart group: ' + group)
    return copy.deepcopy(profile)


def bind_restart_root_guards(scenario, profile):
    state = scenario['steps'][1]
    if state.get('cmd') != 'get-state' or state.get('label') != 'acceptance-owned-state':
        raise ValueError('Restart root guard requires the initial owned get-state observation')
    for key in ('appDataRoot', 'cacheRoot'):
        if not isinstance(profile.get(key), str) or not profile[key]:
            raise ValueError('Predecessor is missing its exact ' + key)
        state['expect'].append({'path': key, 'op': '==', 'value': profile[key]})


def validate_restart_roots(profile, session):
    for key in ('appDataRoot', 'cacheRoot'):
        if not profile.get(key) or not session.get(key) or profile[key] != session[key]:
            raise ValueError('Restart changed the exact predecessor ' + key)


def fixture_contract():
    # Load the adjacent owned helper, never an ambient Python module named fixtures.
    path = Path(__file__).with_name('fixtures.py')
    spec = importlib.util.spec_from_file_location('_acceptance119_fixture_contract', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def validate_seed_paths(value, root, safe_path, key=''):
    if isinstance(value, dict):
        for name, item in value.items():
            validate_seed_paths(item, root, safe_path, str(name))
    elif isinstance(value, list):
        for item in value:
            validate_seed_paths(item, root, safe_path, key)
    elif isinstance(value, str):
        path_key = key.lower() in ('path', 'root', 'file', 'directory') or key.lower().endswith(('_path', '_root', '_file', '_dir'))
        absolute = Path(value).is_absolute() or bool(PureWindowsPath(value).drive)
        if value.startswith('file:'):
            uri = urlsplit(value)
            if uri.netloc:
                raise ValueError('Remote file URI is not an owned fixture path')
            value = unquote(uri.path)
            if os.name == 'nt' and re.match(r'^/[A-Za-z]:', value):
                value = value[1:]
            absolute = True
        if path_key or absolute:
            if not Path(value).is_absolute():
                raise ValueError('Fixture data requires a current absolute local path: ' + value)
            safe_path(root, value)


def validate_fixture_manifest(root, source_sha):
    helpers = fixture_contract()
    root = helpers.assert_owned(Path(root))
    path = helpers.safe_fixture_path(root, 'fixture-manifest.json', must_exist=True)
    manifest = read_json(path)
    if (not isinstance(manifest, dict)
            or manifest.get('schema') != 'colosseum.acceptance119.fixtures.v1'
            or manifest.get('source_sha') != source_sha
            or source_sha != helpers.SOURCE_SHA):
        raise ValueError('Fixture manifest schema/source pin does not match the assigned candidate')
    tokens, seeds = manifest.get('tokens'), manifest.get('seeds')
    if not isinstance(tokens, dict) or not isinstance(seeds, dict):
        raise ValueError('Fixture tokens and seeds must be objects')
    if not isinstance(tokens.get('FIXTURE_ROOT'), str) or not Path(tokens['FIXTURE_ROOT']).is_absolute():
        raise ValueError('Fixture manifest lacks its original absolute FIXTURE_ROOT')
    recorded_root = helpers.safe_fixture_path(root, tokens['FIXTURE_ROOT'], must_exist=True)
    if recorded_root != root:
        raise ValueError('Copied/stale fixture manifest belongs to a different root')
    if manifest.get('root') and helpers.safe_fixture_path(root, manifest['root'], must_exist=True) != root:
        raise ValueError('Fixture manifest recorded root mismatch')
    for key, value in tokens.items():
        if not re.fullmatch(r'[A-Z][A-Z0-9_]*', key) or key in ('SOURCE_ROOT', 'OUTPUT_ROOT'):
            raise ValueError('Invalid/reserved fixture token: ' + str(key))
        if not isinstance(value, (str, int, float)) or isinstance(value, bool):
            raise ValueError('Fixture token must be a scalar string/number: ' + key)
        if isinstance(value, str):
            validate_seed_paths(value, root, helpers.safe_fixture_path, key)
    for name, value in seeds.items():
        if not SAFE_ID.fullmatch(name) or not isinstance(value, str) or not Path(value).is_absolute():
            raise ValueError('Fixture seeds require named absolute current-root paths')
        seed = helpers.safe_fixture_path(root, value, must_exist=True)
        if not seed.is_dir():
            raise ValueError('Fixture seed is not a directory: ' + value)
        for child in seed.rglob('*'):
            checked = helpers.safe_fixture_path(root, child, must_exist=True)
            if checked.is_file() and checked.suffix.lower() == '.json':
                validate_seed_paths(read_json(checked), root, helpers.safe_fixture_path)
    files = manifest.get('files', {})
    if not isinstance(files, dict):
        raise ValueError('Fixture file inventory must be an object')
    for relative in files:
        if not isinstance(relative, str) or Path(relative).is_absolute() or PureWindowsPath(relative).drive:
            raise ValueError('Fixture inventory must use root-relative filenames')
        helpers.safe_fixture_path(root, relative)
    return manifest


def sanitized_env(output):
    env = {k: v for k, v in os.environ.items() if not k.startswith('COLOSSEUM_')}
    if sys.platform.startswith('linux'):
        env.update({key: str(Path(output) / 'profile-environment' / leaf) for key, leaf in (
            ('XDG_DATA_HOME', 'data'), ('XDG_CACHE_HOME', 'cache'), ('XDG_CONFIG_HOME', 'config'))})
        for key in ('XDG_DATA_HOME', 'XDG_CACHE_HOME', 'XDG_CONFIG_HOME'):
            Path(env[key]).mkdir(parents=True, exist_ok=True)
    return env


def observe_identity(exe, qml, env=None):
    exe, qml = Path(exe).resolve(), Path(qml).resolve()
    if not exe.is_file() or not qml.is_file():
        raise ValueError('Candidate executable or QML entrypoint is missing')
    completed = subprocess.run([str(exe), '--version'], capture_output=True, text=True, timeout=15, env=env)
    output = completed.stdout.strip()
    match = re.fullmatch(r'Colosseum ([^\s]+)', output)
    if completed.returncode or not match:
        raise ValueError('Candidate --version failed: ' + output + completed.stderr)
    return {'exe': str(exe), 'exe_sha256': sha256(exe), 'version': match.group(1),
            'qml': str(qml), 'qml_tree_sha256': qml_fingerprint(qml.parent),
            'version_command': [str(exe), '--version'], 'version_exit_code': completed.returncode,
            'version_stdout': completed.stdout, 'version_stderr': completed.stderr}


def attest_identity(identity, observed, manifest):
    if identity.get('source_sha') != manifest['source_sha']:
        raise ValueError('Build attestation source_sha does not match manifest')
    if identity.get('version') != manifest['version']:
        raise ValueError('Build attestation version does not match manifest')
    for key in ('version', 'exe_sha256', 'qml_tree_sha256'):
        if not identity.get(key) or identity[key] != observed[key]:
            raise ValueError('Build attestation mismatch: ' + key)


def source_path(value, source_root):
    path = Path(value)
    return path.resolve() if path.is_absolute() else (Path(source_root) / path).resolve()


def copy_seed(source, destination, tokens):
    if not Path(source).is_dir():
        raise ValueError('Seed directory is unavailable: ' + str(source))
    helper = fixture_contract()
    helper.safe_fixture_path(Path(source), Path(source), must_exist=True)
    for child in Path(source).rglob('*'):
        helper.safe_fixture_path(Path(source), child, must_exist=True)
    shutil.copytree(source, destination, symlinks=False)
    for path in Path(destination).rglob('*.json'):
        write_json(path, materialize(read_json(path), tokens))


def process_snapshot(group):
    """Linux process identities scoped to our newly-created process group."""
    found = {}
    if not sys.platform.startswith('linux'):
        return found
    for path in Path('/proc').glob('[0-9]*/stat'):
        try:
            raw = path.read_text()
            tail = raw[raw.rfind(')') + 2:].split()
            if int(tail[2]) == group:
                found[int(path.parent.name)] = {'start_ticks': tail[19], 'state': tail[0],
                                               'group': group}
        except (OSError, ValueError, IndexError):
            continue
    return found


def cleanup_owned_group(group, owned):
    actions = []
    if not sys.platform.startswith('linux'):
        return actions
    current = process_snapshot(group)
    for pid, identity in current.items():
        if pid in owned and owned[pid]['start_ticks'] == identity['start_ticks'] and identity['state'] != 'Z':
            try:
                os.kill(pid, signal.SIGKILL)
                actions.append({'pid': pid, 'signal': 'SIGKILL', 'reason': 'owned timeout/orphan recovery'})
            except ProcessLookupError:
                pass
    return actions


def execute_native(command, cwd, env, timeout, stdout_path, stderr_path):
    started = time.monotonic()
    with Path(stdout_path).open('w', encoding='utf-8') as stdout, Path(stderr_path).open('w', encoding='utf-8') as stderr:
        process = subprocess.Popen(command, cwd=cwd, env=env, stdout=stdout, stderr=stderr,
                                   start_new_session=os.name != 'nt')
        owned = {}; timed_out = False; actions = []
        while process.poll() is None:
            owned.update(process_snapshot(process.pid))
            if time.monotonic() - started >= timeout:
                timed_out = True
                if os.name == 'nt':
                    # Popen still owns a live process handle. /T limits cleanup to
                    # this launcher tree; no process-name sweep is permitted.
                    killed = subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'],
                                            capture_output=True, text=True, timeout=15)
                    actions.append({'pid': process.pid, 'method': 'taskkill-owned-tree',
                                    'exit_code': killed.returncode})
                else:
                    actions.extend(cleanup_owned_group(process.pid, owned))
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill(); process.wait(timeout=5)
                    actions.append({'pid': process.pid, 'method': 'Popen.kill', 'reason': 'timeout'})
                break
            time.sleep(0.1)
        # Any remaining recorded live helper makes the normal run fail, even if
        # recovering it succeeds. Zombies are exited processes, not live helpers.
        helper_deadline = time.monotonic() + (0 if timed_out else 8)
        while True:
            remaining = process_snapshot(process.pid)
            live = {p: i for p, i in remaining.items() if i['state'] != 'Z'}
            owned.update(live)
            if not live or time.monotonic() >= helper_deadline:
                break
            time.sleep(0.05)
        if live:
            actions.extend(cleanup_owned_group(process.pid, owned))
        return {'returncode': 124 if timed_out else process.returncode,
                'native_returncode': process.returncode, 'timeout': timed_out,
                'duration_seconds': time.monotonic() - started, 'launcher_pid': process.pid,
                'owned_processes': owned, 'remaining_live_processes': live, 'recovery_actions': actions,
                'process_tracking': 'linux-owned-process-group' if sys.platform.startswith('linux')
                                    else 'launcher-only; use separate Windows receiving helper checks'}


def session_manifest(package_root, tag):
    matches = []
    parent = Path(package_root) / 'artifacts' / 'lanista-sessions'
    for path in parent.glob('*/session.json'):
        try:
            data = read_json(path)
            if data.get('tag') == tag:
                matches.append((path, data))
        except (OSError, ValueError):
            continue
    return sorted(matches, key=lambda row: row[0].parent.name)


def run_case(case, context):
    cid = case['id']; output = context['output'] / 'cases' / cid
    output.mkdir(parents=True, exist_ok=False)
    result = {'id': cid, 'layer': case['layer'], 'phase': case.get('phase', 'fresh'),
              'platform': context['platform'], 'source_sha': context['manifest']['source_sha'],
              'identity': context['identity'], 'started_at': utc_now(), 'status': 'NOT RUN',
              'known_limits': list(case.get('limitations', [])), 'failures': []}
    if case.get('blocked_reason'):
        result.update(status='BLOCKED', reason=case['blocked_reason'])
    elif context['platform'] not in case['platforms']:
        result.update(status='NOT RUN', reason='Case requires ' + ', '.join(case['platforms']))
    else:
        try:
            current_fixtures = validate_fixture_manifest(context['fixture_root'], context['manifest']['source_sha'])
            tokens = dict(current_fixtures['tokens'], SOURCE_ROOT=context['source'].as_posix(),
                          FIXTURE_ROOT=context['fixture_root'].as_posix(), OUTPUT_ROOT=output.as_posix())
            scenario_path = source_path(case['scenario'], context['source'])
            prepared = prepare_scenario(materialize(read_json(scenario_path), tokens),
                                        shutdown_grace_ms=context['shutdown_grace_ms'])
            materialized = output / 'scenario.json'; write_json(materialized, prepared)
            group = case.get('profile_group', cid)
            profiles = context['run']['profiles']
            if case.get('phase', 'fresh') == 'restart':
                profile = restart_profile(context['run'], group, context['manifest']['source_sha'],
                                          context['identity']['exe_sha256'], context['identity']['qml_tree_sha256'])
            else:
                if group in profiles:
                    raise ValueError('Fresh phase would overwrite an existing profile group: ' + group)
                profile = {'tag': '119-' + uuid.uuid4().hex, 'completed_fresh': False,
                           'environment_root': str(context['profile_environment_root'])}
                profiles[group] = profile
            tag = profile['tag']
            if case.get('phase') == 'restart':
                bind_restart_root_guards(prepared, profile)
                write_json(materialized, prepared)
            result.update(profile_group=group, tag=tag)
            env = sanitized_env(profile['environment_root'])
            existing = {p.parent.name for p, _ in session_manifest(context['package'], tag)}
            command = [str(context['lanista']), 'session', 'run', str(materialized),
                       '--exe', str(context['exe']), '--qml', str(context['qml']), '--tag', tag,
                       '--drive', '--ready-ms', str(context['ready_ms']), '--timings', '--verbose']
            if case.get('expected_failure'):
                command.append('--keep-going')
            if case.get('mutation_before'):
                mutation_command = [sys.executable, str(context['source'] / 'tests' / 'acceptance_119' / 'fixtures.py'),
                                    '--root', str(context['fixture_root']), '--mutate', case['mutation_before']]
                mutation = subprocess.run(mutation_command, capture_output=True, text=True, timeout=30)
                result['mutation'] = {'command': mutation_command, 'exit_code': mutation.returncode,
                                      'stdout': mutation.stdout, 'stderr': mutation.stderr}
                if mutation.returncode:
                    raise ValueError('Owned fixture mutation failed: ' + mutation.stderr)
            if case.get('seed'):
                seed_value = current_fixtures.get('seeds', {}).get(case['seed'], case['seed'])
                raw_seed = Path(materialize(seed_value, tokens))
                raw_seed = raw_seed if raw_seed.is_absolute() else context['source'] / raw_seed
                helper = fixture_contract()
                if case['seed'] in current_fixtures['seeds']:
                    seed_source = helper.safe_fixture_path(context['fixture_root'], raw_seed, must_exist=True)
                else:
                    seed_source = helper.safe_fixture_path(context['source'], raw_seed, must_exist=True)
                seed = output / 'seed'; copy_seed(seed_source, seed, tokens)
                command += ['--seed', str(seed)]
            deadline = scenario_timeout_seconds(prepared, context['ready_ms'])
            result.update(commands=[command], environment={k: v for k, v in env.items()
                          if k.startswith('XDG_') or k in ('DISPLAY', 'WAYLAND_DISPLAY', 'QT_QPA_PLATFORM')},
                          scenario=str(materialized), scenario_sha256=sha256(materialized), timeout_seconds=deadline,
                          shutdown_grace_ms=context['shutdown_grace_ms'])
            write_json(output / 'result.json', result)
            stdout_path, stderr_path = output / 'lanista.stdout.log', output / 'lanista.stderr.log'
            execution = execute_native(command, context['package'], env, deadline, stdout_path, stderr_path)
            result['execution'] = execution
            replies = parse_replies(stdout_path.read_text(encoding='utf-8', errors='replace'))
            write_json(output / 'protocol.json', replies)
            sessions = [(p, s) for p, s in session_manifest(context['package'], tag) if p.parent.name not in existing]
            if len(sessions) != 1:
                raise ValueError('Expected exactly one newly-created owned session manifest, got ' + str(len(sessions)))
            path, session = sessions[0]
            restart_failures = []
            if case.get('phase') == 'restart':
                try:
                    validate_restart_roots(profile, session)
                except ValueError as error:
                    restart_failures.append(str(error))
            result['session'] = session
            result['session_artifacts'] = str(output / 'session')
            shutil.copytree(path.parent, output / 'session')
            failures = adjudicate_session(execution['returncode'], session, replies, tag,
                                          context['exe'], context['identity']['exe_sha256'])
            failures.extend(restart_failures)
            if case.get('expected_failure'):
                oracle_failures = expected_failure_verdict(case['expected_failure'], execution['returncode'],
                    stdout_path.read_text(encoding='utf-8', errors='replace'), replies, prepared['steps'])
                if not oracle_failures:
                    failures = [f for f in failures if f != 'Lanista exited with code 1']
                failures.extend(oracle_failures)
                result['negative_control'] = {'expected': case['expected_failure'],
                    'native_status': 'FAIL' if execution['returncode'] == 1 else 'UNEXPECTED',
                    'oracle_status': 'FAIL' if oracle_failures else 'PASS'}
            if execution['recovery_actions'] or execution['remaining_live_processes']:
                failures.append('Owned processes required failure recovery or survived normal shutdown')
            evidence, evidence_failures = collect_app_evidence(session, replies, output / 'app-evidence')
            failures += evidence_failures
            if not any(p.lower().endswith('.png') for p in evidence):
                failures.append('No readable rendered screenshot evidence was preserved')
            result.update(status='FAIL' if failures else 'PASS', failures=failures,
                          evidence=evidence, protocol=str(output / 'protocol.json'))
            timing_path = output / 'session' / 'timings.json'
            if timing_path.is_file():
                result['timings'] = read_json(timing_path)
            else:
                result['failures'].append('Native timing artifact is missing'); result['status'] = 'FAIL'
            profile.update(last_session_id=session.get('sessionId'), appDataRoot=session.get('appDataRoot'),
                           cacheRoot=session.get('cacheRoot'), last_case=cid, last_status=result['status'])
            if case.get('phase', 'fresh') == 'fresh':
                profile['completed_fresh'] = result['status'] == 'PASS'
            profiles[group] = profile
            if os.name == 'nt':
                result['known_limits'].append('Windows helper-process cleanup requires the separate receiving runner')
        except (OSError, ValueError, subprocess.SubprocessError) as error:
            result.update(status='FAIL', reason=str(error))
            result['failures'].append(str(error))
    result['ended_at'] = utc_now()
    write_json(output / 'result.json', result)
    return result


def parser():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--manifest', type=Path, default=Path(__file__).with_name('manifest.json'))
    p.add_argument('--package-root', type=Path)
    p.add_argument('--source-root', type=Path, default=Path(__file__).resolve().parents[2])
    p.add_argument('--lanista', type=Path)
    p.add_argument('--exe', type=Path, required=True)
    p.add_argument('--qml', type=Path, required=True)
    p.add_argument('--output-root', type=Path)
    p.add_argument('--fixture-root', type=Path)
    p.add_argument('--identity', type=Path)
    p.add_argument('--inspect', action='store_true')
    selection = p.add_mutually_exclusive_group()
    selection.add_argument('--case', action='append', default=[])
    selection.add_argument('--full', action='store_true')
    p.add_argument('--phase', choices=('fresh', 'restart', 'all'), default='all')
    p.add_argument('--resume', type=Path)
    p.add_argument('--expected-app-sha256')
    p.add_argument('--qml-manifest', type=Path)
    p.add_argument('--ready-ms', type=int, default=60000)
    p.add_argument('--shutdown-grace-ms', type=int, default=DEFAULT_SHUTDOWN_GRACE_MS,
                   help='Separate final normal-quit grace before native teardown: 500..20000 ms (default: 15000)')
    return p


def main(argv=None):
    args = parser().parse_args(argv)
    try:
        if args.inspect:
            print(json.dumps(observe_identity(args.exe, args.qml), indent=2)); return 0
        if not all((args.package_root, args.lanista, args.output_root, args.fixture_root, args.identity)):
            raise ValueError('Execution requires package, Lanista, new output, fixture root, and reviewed identity')
        if not args.case and not args.full:
            raise ValueError('Select --case ID or --full explicitly')
        if not 1 <= args.ready_ms <= 120000:
            raise ValueError('ready-ms must be in [1,120000]')
        validate_shutdown_grace(args.shutdown_grace_ms)
        manifest = read_json(args.manifest); selected = select_cases(manifest, args.case, args.phase)
        output = args.output_root.resolve()
        if output.exists():
            raise ValueError('Output root already exists; use a fresh directory')
        output.mkdir(parents=True)
        env = sanitized_env(output)
        observed = observe_identity(args.exe, args.qml, env)
        identity = read_json(args.identity); attest_identity(identity, observed, manifest)
        if args.expected_app_sha256 and observed['exe_sha256'] != args.expected_app_sha256:
            raise ValueError('Explicit expected app SHA256 does not match')
        if args.qml_manifest:
            fields = dict(line.split('=', 1) for line in args.qml_manifest.read_text().splitlines() if '=' in line)
            if fields.get('schema') != '1' or fields.get('qmlTreeSha256') != observed['qml_tree_sha256']:
                raise ValueError('Packaged qml-build.manifest does not match observed QML')
        require_png_decoder()
        fixtures = validate_fixture_manifest(args.fixture_root, manifest['source_sha'])
        tokens = dict(fixtures.get('tokens', {}))
        tokens.update(SOURCE_ROOT=args.source_root.resolve().as_posix(),
                      FIXTURE_ROOT=args.fixture_root.resolve().as_posix(), OUTPUT_ROOT=output.as_posix())
        plat = 'windows' if os.name == 'nt' else ('macos' if sys.platform == 'darwin' else 'linux')
        run = {'schema': RUN_SCHEMA, 'ownership_token': uuid.uuid4().hex, 'source_sha': manifest['source_sha'],
               'started_at': utc_now(), 'identity': identity, 'observed_identity': observed,
               'shutdown_grace_ms': args.shutdown_grace_ms,
               'manifest': str(args.manifest.resolve()), 'manifest_sha256': sha256(args.manifest),
               'fixture_manifest_sha256': sha256(args.fixture_root / 'fixture-manifest.json'),
               'output_root': str(output), 'runtime': {'platform': plat, 'system': platform.platform(),
               'python': platform.python_version()}, 'profiles': {}, 'cases': [{'id': c['id'], 'layer': c['layer'], 'status': 'NOT RUN',
               'reason': 'Awaiting selected case execution'} for c in selected]}
        profile_root = output
        if args.resume:
            prior = read_json(args.resume)
            prior_output = Path(prior.get('output_root', '')).resolve()
            marker = prior_output / '.acceptance119-owner.json'
            if not marker.is_file() or read_json(marker).get('ownership_token') != prior.get('ownership_token'):
                raise ValueError('Prior run ownership marker is missing or inconsistent')
            for case in selected:
                if case.get('phase') == 'restart' and not case.get('blocked_reason'):
                    group = case['profile_group']
                    run['profiles'][group] = restart_profile(prior, group, manifest['source_sha'], identity['exe_sha256'], identity['qml_tree_sha256'])
            run['restart_predecessor'] = str(args.resume.resolve())
        write_json(output / '.acceptance119-owner.json', {'ownership_token': run['ownership_token']})
        write_json(output / 'run.json', run)
        context = {'output': output, 'source': args.source_root.resolve(), 'package': args.package_root.resolve(),
                   'fixture_root': args.fixture_root.resolve(), 'fixtures': fixtures, 'tokens': tokens,
                   'identity': identity, 'manifest': manifest, 'run': run, 'platform': plat,
                   'exe': args.exe.resolve(), 'qml': args.qml.resolve(), 'lanista': args.lanista.resolve(),
                   'ready_ms': args.ready_ms, 'shutdown_grace_ms': args.shutdown_grace_ms,
                   'profile_environment_root': profile_root}
        for index, case in enumerate(selected):
            result = run_case(case, context)
            run['cases'][index] = result
            run['summary'] = summarize(run['cases']); write_json(output / 'run.json', run)
            print(case['id'] + ': ' + result['status'], flush=True)
        run['ended_at'] = utc_now(); run['summary'] = summarize(run['cases'])
        write_json(output / 'run.json', run)
        print(json.dumps(run['summary'], indent=2)); return run['summary']['exit_code']
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        report = {'status': 'BLOCKED', 'qualified': False, 'reason': str(error), 'observed_at': utc_now()}
        if args.output_root and args.output_root.exists():
            # Never write into an unrelated pre-existing output root.
            if 'output' in locals() and 'env' in locals():
                write_json(args.output_root / 'preflight-blocked.json', report)
                if 'selected' in locals():
                    cases = [{'id': c['id'], 'layer': c['layer'], 'status': 'BLOCKED',
                              'reason': c.get('blocked_reason') or str(error)} for c in selected]
                    write_json(args.output_root / 'run.json', {
                        'schema': RUN_SCHEMA, 'source_sha': manifest['source_sha'], 'cases': cases,
                        'summary': summarize(cases), 'preflight': report})
        print(json.dumps(report, indent=2), file=sys.stderr); return 4


if __name__ == '__main__':
    raise SystemExit(main())
