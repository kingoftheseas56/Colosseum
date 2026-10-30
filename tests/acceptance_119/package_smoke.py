#!/usr/bin/env python3
"""Linux normal-AppRun smoke, using existing native Lanista and acceptance helpers.

No QML override, socket implementation, public profile, or default pipe. Run from
an actual desktop terminal. This complements, rather than replaces, run.py.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import uuid

import run as acceptance


def isolated_env(output, tag, pipe, inherited=None):
    env = {k: v for k, v in (os.environ if inherited is None else inherited).items()
           if not k.startswith(('COLOSSEUM_', 'QT', 'QML_', 'LD_')) and k not in ('QTDIR', 'QT_HOME')}
    for key, leaf in (('HOME', 'home'), ('XDG_DATA_HOME', 'data'), ('XDG_CONFIG_HOME', 'config'), ('XDG_CACHE_HOME', 'cache')):
        path = Path(output) / 'profile' / leaf; path.mkdir(parents=True, exist_ok=True); env[key] = str(path)
    env.update(COLOSSEUM_APPDATA_TAG=tag, COLOSSEUM_LANISTA_PIPE=pipe, COLOSSEUM_LANISTA_DRIVE='1')
    return env


def write_window_settings(config, tag, width, height):
    if width < 1024 or height < 640:
        raise ValueError('Supported normal window minimum is 1024x640')
    path = Path(config) / 'Brotherhood' / ('Colosseum-dltest-' + tag + '.conf')
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text('[window]\nbaseMode=windowed\nfillPolicy=2\nmaximized=false\nnormalGeometry=@Rect(40 40 %d %d)\n' % (width, height))
    return path


def ping_owned(ping, pid, pipe):
    return (ping.get('type') == 'reply' and ping.get('schema') == 'colosseum.dev.v1.0'
            and ping.get('pid') == pid and ping.get('pipe') == pipe
            and ping.get('gates', {}).get('drive') is True and ping.get('gates', {}).get('write') is False)


def cleanup_bridge_allowed(verified, pid, binary, expected_hash):
    if not verified:
        return False
    try:
        live_exe = Path('/proc') / str(pid) / 'exe'
        return live_exe.resolve() == Path(binary).resolve() and acceptance.sha256(live_exe) == expected_hash
    except OSError:
        return False


def ownership_failures(ping, state, pid, tag, pipe, env, width=None, height=None, require_window=True):
    failures = []
    if not ping_owned(ping, pid, pipe):
        failures.append('Owned ping identity mismatch')
    if state.get('type') != 'reply': failures.append('Missing get-state reply')
    for key, variable in (('appDataRoot', 'XDG_DATA_HOME'), ('cacheRoot', 'XDG_CACHE_HOME')):
        path = Path(state.get(key, ''))
        if not acceptance.inside(path, env[variable]) or path.name != 'Colosseum-dltest-' + tag:
            failures.append('Unowned resolved ' + key)
    windows = state.get('windows', [])
    if require_window and (not windows or windows[0].get('visible') is not True): failures.append('No visible native window')
    if width and (not windows or windows[0].get('width') != width or windows[0].get('height') != height):
        failures.append('Actual window dimensions differ from requested viewport')
    return failures


def prepare(scenario, width=None, height=None):
    prepared = acceptance.prepare_scenario(scenario)
    if any('capture' in s for s in prepared['steps']):
        raise ValueError('Session-only video capture is unsupported by normal-AppRun smoke')
    if width:
        prepared['steps'].insert(2, {'label': 'package-actual-viewport', 'cmd': 'get-state', 'expect': acceptance.reply_expect() + [
            {'path': 'windows.0.width', 'op': '==', 'value': width},
            {'path': 'windows.0.height', 'op': '==', 'value': height},
            {'path': 'windows.0.visible', 'op': '==', 'value': True}]})
    acceptance.validate_scenario(prepared)
    return prepared


def scenario_command(lanista, pipe, materialized, keep_going=False):
    command = [str(lanista), '--pipe', pipe, '--verbose', '--timings', 'run', str(materialized)]
    return command + (['--keep-going'] if keep_going else [])


def verdict(launched, failures):
    return 'BLOCKED' if not launched else ('FAIL' if failures else 'PASS')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('package-root', 'source-root', 'output-root', 'scenario'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--fixture-root', type=Path)
    parser.add_argument('--seed', type=Path)
    parser.add_argument('--expected-app-sha256')
    parser.add_argument('--width', type=int); parser.add_argument('--height', type=int)
    parser.add_argument('--ready-seconds', type=float, default=60)
    parser.add_argument('--timeout-seconds', type=float, default=600)
    parser.add_argument('--shutdown-seconds', type=float, default=15)
    parser.add_argument('--keep-going', action='store_true', help='Continue native scenario steps after failures; failed assertions still make the result FAIL')
    args = parser.parse_args(argv)
    output = args.output_root.resolve(); package = args.package_root.resolve(); source = args.source_root.resolve()
    if output.exists():
        print(json.dumps({'status': 'BLOCKED', 'reason': 'Output root must be fresh'})); return 4
    output.mkdir(parents=True)
    result = {'schema': 'colosseum.acceptance119.package-smoke.v1', 'status': 'NOT RUN', 'layer': 'Linux real normal AppRun plus native Lanista',
              'started_at': acceptance.utc_now(), 'commands': [], 'failures': [], 'recovery_actions': [], 'qml_override': False}
    process = None; app_log = None; env = None; state = {}; replies = []; owned = {}; pipe = ''; tag = ''; lanista = None; bridge_verified = False
    def cli(command, timeout=3):
        call = [str(lanista), '--pipe', pipe, '--timeout', str(int(max(0.1, timeout - 1) * 1000))] + command
        done = subprocess.run(call, env=env, cwd=package, capture_output=True, text=True, timeout=timeout)
        row = {'command': call, 'exit_code': done.returncode, 'stdout': done.stdout, 'stderr': done.stderr}
        result['commands'].append(row)
        try: response = json.loads(done.stdout)
        except ValueError: response = {}
        return done.returncode, response
    try:
        if not sys.platform.startswith('linux'): raise ValueError('This normal-AppRun helper is Linux-only')
        if bool(args.width) != bool(args.height): raise ValueError('Supply width and height together')
        if not all(0 < v <= 3600 for v in (args.ready_seconds, args.timeout_seconds, args.shutdown_seconds)):
            raise ValueError('Timeouts must be positive and at most 3600 seconds')
        tag = '119-package-' + uuid.uuid4().hex; pipe = 'ColosseumLanista-' + tag
        env = isolated_env(output, tag, pipe)
        if not env.get('DISPLAY') and not env.get('WAYLAND_DISPLAY'):
            raise ValueError('A real desktop DISPLAY or WAYLAND_DISPLAY is required')
        binary = package / 'usr/bin/colosseum'; lanista = package / 'usr/bin/lanista'; app_run = package / 'AppRun'
        identity = acceptance.read_json(package / 'BUILD-IDENTITY.json')
        binary_hash = acceptance.sha256(binary); qml_hash = acceptance.qml_fingerprint(package / 'qml')
        if identity.get('version') != '1.1.9' or identity.get('binary_sha256') != binary_hash:
            raise ValueError('Packaged version/executable identity mismatch')
        if args.expected_app_sha256 and args.expected_app_sha256 != binary_hash: raise ValueError('Explicit app hash mismatch')
        if identity.get('lanista_sha256') != acceptance.sha256(lanista): raise ValueError('Packaged Lanista identity mismatch')
        if identity.get('qml_tree_sha256') != qml_hash or 'qmlTreeSha256=' + qml_hash not in (package / 'usr/bin/qml-build.manifest').read_text():
            raise ValueError('Packaged QML identity mismatch')
        result.update(package_root=str(package), source_sha=identity['source_commit'], app_sha256=binary_hash,
                      app_run_sha256=acceptance.sha256(app_run), qml_tree_sha256=qml_hash, tag=tag, pipe=pipe,
                      profile={k: env[k] for k in ('HOME', 'XDG_DATA_HOME', 'XDG_CONFIG_HOME', 'XDG_CACHE_HOME')})
        version = subprocess.run([str(app_run), '--version'], env=env, cwd=output, capture_output=True, text=True, timeout=15)
        result['commands'].append({'command': [str(app_run), '--version'], 'exit_code': version.returncode, 'stdout': version.stdout, 'stderr': version.stderr})
        if version.returncode or version.stdout.strip() != 'Colosseum 1.1.9': raise ValueError('Normal AppRun version failed')
        acceptance.require_png_decoder()
        fixtures = acceptance.validate_fixture_manifest(args.fixture_root, identity['source_commit']) if args.fixture_root else {}
        tokens = dict(fixtures.get('tokens', {}))
        tokens.update(SOURCE_ROOT=source.as_posix(), OUTPUT_ROOT=output.as_posix())
        if args.fixture_root: tokens['FIXTURE_ROOT'] = args.fixture_root.resolve().as_posix()
        scenario = acceptance.source_path(args.scenario, source)
        prepared = prepare(acceptance.materialize(acceptance.read_json(scenario), tokens), args.width, args.height)
        materialized = output / 'scenario.json'; acceptance.write_json(materialized, prepared)
        result.update(scenario=str(scenario), materialized_scenario_sha256=acceptance.sha256(materialized))
        if args.seed:
            if not args.fixture_root: raise ValueError('Seed requires a validated owned fixture root')
            seed = acceptance.source_path(fixtures['seeds'].get(str(args.seed), str(args.seed)), source)
            if seed not in {Path(p).resolve() for p in fixtures['seeds'].values()}:
                raise ValueError('Seed must be declared by the validated fixture manifest')
            acceptance.fixture_contract().safe_fixture_path(args.fixture_root, seed, must_exist=True)
            acceptance.copy_seed(seed, Path(env['XDG_DATA_HOME']) / 'Brotherhood' / ('Colosseum-dltest-' + tag), tokens)
        if args.width: result['window_settings'] = str(write_window_settings(env['XDG_CONFIG_HOME'], tag, args.width, args.height))
        app_log = (output / 'app.log').open('w')
        process = subprocess.Popen([str(app_run)], cwd=output, env=env, stdout=app_log, stderr=subprocess.STDOUT, start_new_session=True)
        result.update(pid=process.pid, launch_command=[str(app_run)], launch_cwd=str(output))
        deadline = time.monotonic() + args.ready_seconds; ping = {}; ready_failures = ['No valid exchange']
        while process.poll() is None and time.monotonic() < deadline:
            owned.update(acceptance.process_snapshot(process.pid))
            rc, ping = cli(['ping'])
            if rc == 0:
                if not ping_owned(ping, process.pid, pipe): raise ValueError('Owned ping identity mismatch')
                rc, state = cli(['get-state'])
                result.update(ping=ping, initial_state=state)
                security_failures = ownership_failures(ping, state, process.pid, tag, pipe, env, require_window=False)
                if security_failures: raise ValueError('; '.join(security_failures))
                if not cleanup_bridge_allowed(True, process.pid, binary, binary_hash):
                    raise ValueError('Live owned PID executable path/hash mismatch')
                bridge_verified = True
                result['bridge_verified'] = True
                ready_failures = ownership_failures(ping, state, process.pid, tag, pipe, env, args.width, args.height)
                if not ready_failures: break
            time.sleep(0.1)
        else: raise ValueError('Native readiness failed: ' + '; '.join(ready_failures))
        result.update(ping=ping, initial_state=state)
        live_exe = Path('/proc') / str(process.pid) / 'exe'
        if live_exe.resolve() != binary.resolve() or acceptance.sha256(live_exe) != binary_hash:
            raise ValueError('Live owned PID executable path/hash mismatch')
        result['live_executable_verified'] = True
        command = scenario_command(lanista, pipe, materialized, args.keep_going)
        result['scenario_command'] = command
        execution = acceptance.execute_native(command, output, env, args.timeout_seconds, output / 'lanista.stdout.log', output / 'lanista.stderr.log')
        result['execution'] = execution; owned.update(acceptance.process_snapshot(process.pid))
        replies = acceptance.parse_replies((output / 'lanista.stdout.log').read_text(errors='replace'))
        if execution['returncode'] != 0: result['failures'].append('Native scenario exit ' + str(execution['returncode']))
        if execution['recovery_actions'] or execution['remaining_live_processes']: result['failures'].append('Native CLI required forced recovery')
        quit_reply = next((r['response'] for r in replies if r['label'] == 'acceptance-normal-quit'), {})
        if quit_reply.get('type') != 'reply' or quit_reply.get('pressed') != 'Ctrl+Q': result['failures'].append('Scenario did not acknowledge normal quit')
    except (OSError, ValueError, subprocess.SubprocessError, KeyboardInterrupt) as error:
        result['failures'].append(str(error) or type(error).__name__)
    finally:
        if process is not None:
            shutdown_started = time.monotonic()
            if (result['failures'] and process.poll() is None
                    and cleanup_bridge_allowed(bridge_verified, process.pid, binary, binary_hash)):
                try:
                    _, snapshot = cli(['get-state', '--grab', 'window'], timeout=6)
                    replies.append({'label': 'package-failure-state', 'response': snapshot})
                    _, quit_reply = cli(['ui-keypress', 'key=Ctrl+Q'])
                    result['failure_cleanup_quit'] = quit_reply
                except (OSError, subprocess.SubprocessError) as error: result['failures'].append('Failure cleanup: ' + str(error))
            deadline = time.monotonic() + args.shutdown_seconds
            while process.poll() is None and time.monotonic() < deadline:
                owned.update(acceptance.process_snapshot(process.pid)); time.sleep(0.1)
            if process.poll() is None:
                result['failures'].append('Forced recovery after shutdown deadline')
                result['recovery_actions'] += acceptance.cleanup_owned_group(process.pid, owned)
                if process.poll() is None: process.kill(); result['recovery_actions'].append({'pid': process.pid, 'signal': 'SIGKILL', 'reason': 'owned app shutdown timeout'})
                process.wait(timeout=5)
            result['app_exit_code'] = process.returncode
            if process.returncode != 0: result['failures'].append('App did not exit normally with code zero')
            helper_deadline = time.monotonic() + 8
            while True:
                live = {p:i for p,i in acceptance.process_snapshot(process.pid).items() if i['state'] != 'Z'}
                owned.update(live)
                if not live or time.monotonic() >= helper_deadline: break
                time.sleep(0.1)
            result['remaining_live_processes'] = live
            if live:
                result['failures'].append('Owned helpers survived normal shutdown')
                result['recovery_actions'] += acceptance.cleanup_owned_group(process.pid, owned)
            result['owned_processes'] = owned
            result['shutdown_duration_seconds'] = time.monotonic() - shutdown_started
        if app_log: app_log.close()
        if state.get('appDataRoot') and env and not ownership_failures(result.get('ping', {}), state, process.pid, tag, pipe, env, require_window=False):
            evidence, failures = acceptance.collect_app_evidence(dict(state, tag=tag), replies, output / 'app-evidence')
            result['evidence'] = evidence; result['failures'] += failures
            if not any(p.endswith('.png') for p in evidence): result['failures'].append('No rendered screenshot evidence')
        acceptance.write_json(output / 'protocol.json', replies)
        result.update(status=verdict(process is not None, result['failures']), ended_at=acceptance.utc_now())
        acceptance.write_json(output / 'result.json', result)
    print(json.dumps({'status': result['status'], 'failures': result['failures'], 'result': str(output / 'result.json')}, indent=2))
    return {'PASS': 0, 'FAIL': 1, 'BLOCKED': 4}[result['status']]


if __name__ == '__main__': raise SystemExit(main())
