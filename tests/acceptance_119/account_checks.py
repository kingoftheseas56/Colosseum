#!/usr/bin/env python3
"""Thin, fail-closed launcher for existing 1.1.9 account/trackers/lifecycle tests.

No alternate service or UI framework is implemented here. PostgreSQL tests always
use a newly initialized, loopback-only disposable cluster; caller database URLs
are deliberately not accepted. Desktop refresh trials retain their Windows-only
real credential-vault contract. Qt fixture tests are labeled modeled components.
"""
from __future__ import annotations

import argparse
import contextlib
import hashlib
import json
import os
import platform
import shutil
import signal
import socket
import subprocess
import tempfile
import time
import uuid
import xml.etree.ElementTree as ET
from pathlib import Path

SOURCE = Path(__file__).resolve().parents[2]
REFRESH = ['normal_restart', 'lost_response_automatic_retry',
           'lost_response_quick_restart', 'lost_response_late_restart']
CONFIG = ['TestLoadRequiresProductionAvatarStorage',
          'TestLoadAllowsExplicitlyDisabledProductionAvatarStorage',
          'TestLoadDisabledProductionAvatarStorageStillRequiresSecurityKeys']
SERVICE = ['TestRefreshResponseLossRecoversAfterTimeoutAndRestart',
           'TestRefreshResponseLossRecoveryHonorsRevocation',
           'TestRefreshRotationRetriesWithinGraceAndRevokesOnLateReplay']
NATIVE = {
    'account-native': ('tst_account_identity', [
        'credentialTargetNamesAreIsolatedByAppDataTag', 'pendingCredentialTargetsMatchOnlyTheirExactNamespace',
        'localOnlyChoiceSurvivesRestart', 'rememberedSessionRestartRotatesSecureCredential',
        'offlineRestoreKeepsRememberedCredential', 'offlineRestartRestoresRememberedIdentity',
        'revokedRestoreClearsCredentialAndLocks', 'avatarMutationFailurePreservesPersistedAvatar']),
    'tracker-native': ('tst_tracker_connection_foundation', [
        'zeroTrackerStateCreatesNoFileOrTransportTraffic', 'builtInCatalogueRemainsFailClosed',
        'connectionReopensWithoutTouchingCanonicalHistory', 'productionRegistryContainsNoTestTransport']),
    'tracker-teardown': ('tst_tracker_lifecycle', [
        'connectionRollbackSurvivesControllerTeardown', 'connectionDeactivationGateRollsBackPendingCredential']),
    'watchparty-shutdown': ('tst_watchparty_ui', [
        'owned_account_bridge_lifetime_matches_ui_controller',
        'earlier_created_sibling_sync_can_be_destroyed_first', 'retired_sync_cannot_receive_room_actions']),
    'watchparty-room-end': ('tst_watchparty_room', ['explicit_leave_and_end_destroy_ephemeral_room_state']),
    'tracker-ui': ('colosseum_qml_tests', [
        'test_empty_state_uses_native_unavailable_catalogue',
        'test_trakt_uses_stremio_link_and_future_cards_say_coming_soon']),
    'watchparty-lifecycle': ('tst_watchparty_lifecycle', [
        'terminal_room_end_clears_ephemeral_presentation', 'participant_removal_terminates_current_membership']),
}
CASES = ['avatar-config', 'account-service', 'windows-refresh', *NATIVE]


def go_verdict(log, exit_code, required):
    events = []
    for line in log.splitlines():
        try:
            event = json.loads(line)
            if isinstance(event, dict):
                events.append(event)
        except ValueError:
            pass
    failed = [e.get('Test', 'package') for e in events if e.get('Action') == 'fail']
    if exit_code != 0 or failed:
        return 'FAIL', {'failed': failed, 'exit_code': exit_code}
    passed = {e.get('Test') for e in events if e.get('Action') == 'pass'}
    skipped = [e.get('Test', 'package') for e in events if e.get('Action') == 'skip']
    missing = [name for name in required if name not in passed]
    return ('BLOCKED' if skipped or missing else 'PASS'), {'passed': sorted(p for p in passed if p),
                                                          'skipped': skipped, 'missing': missing}


def qt_verdict(log, exit_code, required):
    try:
        root = ET.fromstring(log[log.index('<?xml'):]) if '<?xml' in log else ET.fromstring(log)
    except (ValueError, ET.ParseError):
        return 'FAIL', {'reason': 'Qt XML evidence missing or malformed', 'exit_code': exit_code}
    functions = {n.attrib.get('name'): n for n in root.findall('TestFunction')}
    failures = [n.attrib.get('name') for n in functions.values()
                if any(e.attrib.get('type') in ('fail', 'xpass') for e in n.findall('Incident'))]
    skips = [n.attrib.get('name') for n in functions.values()
             if n.findall('Message[@type="skip"]') or n.findall('Incident[@type="skip"]')]
    missing = [name for name in required if name not in functions or
               not functions[name].findall('Incident[@type="pass"]')]
    status = 'FAIL' if exit_code != 0 or failures else 'BLOCKED' if skips or missing else 'PASS'
    return status, {'failed': failures, 'skipped': skips, 'missing': missing,
                    'passed': [name for name in required if name not in missing]}


def run_command(command, cwd, env, log, timeout=180):
    started = time.monotonic()
    kwargs = {'start_new_session': True} if os.name != 'nt' else {
        'creationflags': subprocess.CREATE_NEW_PROCESS_GROUP}
    with Path(log).open('w', encoding='utf-8') as stream:
        child = subprocess.Popen([str(a) for a in command], cwd=cwd, env=env,
                                 stdout=stream, stderr=subprocess.STDOUT, **kwargs)
        forced = False
        try:
            child.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            forced = True
            # This process group contains only descendants we just created.
            if os.name == 'nt':
                subprocess.run(['taskkill', '/PID', str(child.pid), '/T', '/F'],
                               stdout=stream, stderr=subprocess.STDOUT, timeout=15, check=False)
            else:
                os.killpg(child.pid, signal.SIGKILL)
            child.wait(timeout=15)
    return {'command': [str(a) for a in command], 'pid': child.pid, 'exit_code': child.returncode,
            'elapsed_seconds': round(time.monotonic() - started, 3), 'forced_kill': forced,
            'log': str(Path(log).resolve())}


def sha256(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def executable(root, name):
    suffix = '.exe' if os.name == 'nt' else ''
    for folder in (Path(root), Path(root) / 'Release', Path(root) / 'tests'):
        candidate = folder / (name + suffix)
        if candidate.is_file():
            return candidate.resolve()
    raise FileNotFoundError(f'{name}{suffix} is missing under {root}')


@contextlib.contextmanager
def disposable_database(pg_bin, root, env, evidence, commands):
    """Own the cluster, database and PID. Never reuse caller data or DB settings."""
    initdb, postgres, createdb, pg_ctl = [executable(pg_bin, n) for n in
                                         ('initdb', 'postgres', 'createdb', 'pg_ctl')]
    data = root / 'postgres-data'
    init = run_command([initdb, '-D', data, '-U', 'acceptance', '-A', 'trust',
                        '--no-locale', '--encoding=UTF8'], root, env, evidence / 'initdb.log', 90)
    commands.append(init)
    if init['exit_code'] != 0:
        raise RuntimeError('disposable initdb failed; see initdb.log')
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        port = sock.getsockname()[1]
    postgres_log = evidence / 'postgres.log'
    stream = postgres_log.open('w', encoding='utf-8')
    command = [str(postgres), '-D', str(data), '-h', '127.0.0.1', '-p', str(port),
               '-c', 'unix_socket_directories=']
    child = subprocess.Popen(command, cwd=root, env=env, stdout=stream, stderr=subprocess.STDOUT)
    record = {'command': command, 'pid': child.pid, 'log': str(postgres_log), 'forced_kill': False}
    commands.append(record)
    try:
        ready = False
        deadline = time.monotonic() + 20
        while child.poll() is None and time.monotonic() < deadline:
            # A free-port race must not let us use somebody else's listener.
            pid_file = data / 'postmaster.pid'
            try:
                lines = pid_file.read_text().splitlines()
                ready = (int(lines[0]) == child.pid and lines[-1].strip() == 'ready')
            except (OSError, ValueError, IndexError):
                pass
            if ready:
                break
            time.sleep(.1)
        if not ready:
            raise RuntimeError('owned PostgreSQL did not become ready')
        db_name = 'acceptance_' + uuid.uuid4().hex + '_test'
        create = run_command([createdb, '-h', '127.0.0.1', '-p', str(port), '-U',
                              'acceptance', db_name], root, env, evidence / 'createdb.log', 20)
        commands.append(create)
        if create['exit_code'] != 0:
            raise RuntimeError('could not create isolated test database')
        yield f'postgres://acceptance@127.0.0.1:{port}/{db_name}?sslmode=disable'
    finally:
        if child.poll() is None:
            stop = run_command([pg_ctl, '-D', data, '-m', 'fast', '-w', '-t', '15', 'stop'],
                               root, env, evidence / 'postgres-stop.log', 20)
            commands.append(stop)
            try:
                child.wait(timeout=5)
            except subprocess.TimeoutExpired:
                record['forced_kill'] = True
                child.kill()
                child.wait(timeout=10)
        record['exit_code'] = child.returncode
        record['cleanup_verified'] = child.poll() is not None
        stream.close()


def run_case(case, args, env, profile, evidence):
    result = {'case_id': case, 'status': 'NOT RUN', 'commands': [],
              'fixture': 'disposable synthetic data only', 'profile': str(profile),
              'layer': 'modeled Qt component', 'assertions': {}}
    commands = result['commands']
    try:
        if case in NATIVE:
            target, required = NATIVE[case]
            binary = executable(args.build_root, target)
            result['executable_sha256'] = sha256(binary)
            log = evidence / f'{case}.xml'
            arguments = [*required]
            if case == 'tracker-ui':
                result['layer'] = 'focused production QML; native unavailable catalogue and fixture models'
                required = ['TrackerSyncCenterPage::' + name for name in required]
                arguments = ['-input', str(SOURCE / 'tests/qml/tracker_sync_center/tst_tracker_sync_center_page.qml'),
                             *required]
            run = run_command([binary, *arguments, '-o', '-,xml'], SOURCE, env, log)
            commands.append(run)
            result['status'], result['assertions'] = qt_verdict(log.read_text(), run['exit_code'], required)
        else:
            result['layer'] = {'avatar-config': 'actual Go configuration',
                               'account-service': 'actual PostgreSQL service; modeled clock',
                               'windows-refresh': 'actual Windows Qt HTTP/vault + PostgreSQL'}[case]
            if case == 'windows-refresh' and os.name != 'nt':
                raise FileNotFoundError('real desktop refresh requires Windows credential vault; Linux cannot qualify it')
            go = shutil.which(args.go) or (args.go if Path(args.go).is_file() else None)
            if not go:
                raise FileNotFoundError('Go 1.26.6 toolchain unavailable')
            go = str(Path(go).resolve())
            version = run_command([go, 'version'], SOURCE, env, evidence / f'{case}-go-version.log', 30)
            commands.append(version)
            if version['exit_code'] != 0 or 'go version go1.' not in Path(version['log']).read_text():
                raise FileNotFoundError('configured executable is not a working Go toolchain')
            result['runtime'] = Path(version['log']).read_text().strip()
            if case == 'avatar-config':
                required, package = CONFIG, './internal/config'
            elif case == 'account-service':
                required, package = SERVICE, './internal/account'
            else:
                binary = executable(args.build_root, 'tst_account_identity')
                result['executable_sha256'] = sha256(binary)
                env['COLOSSEUM_REFRESH_REPRO_EXE'] = str(binary)
                selected = [args.refresh_scenario] if args.refresh_scenario else REFRESH
                required = ['TestDesktopRefreshResponseLoss/' + name for name in selected]
                package = './internal/account'
            selector = '^(' + '|'.join(sorted({n.split('/')[0] for n in required})) + ')$'
            if case == 'windows-refresh' and args.refresh_scenario:
                selector += '/^' + args.refresh_scenario + '$'
            log = evidence / f'{case}.jsonl'
            command = [go, 'test', '-json', '-count=1', '-timeout=6m', '-run', selector, package]
            def invoke():
                run = run_command(command, SOURCE / 'server/account-service', env, log, 390)
                commands.append(run)
                result['status'], result['assertions'] = go_verdict(log.read_text(), run['exit_code'], required)
            if case == 'avatar-config':
                invoke()
            else:
                if not args.pg_bin:
                    raise FileNotFoundError('--pg-bin is required for a new isolated PostgreSQL cluster')
                with disposable_database(args.pg_bin, profile, env, evidence, commands) as url:
                    env['TEST_DATABASE_URL'] = url
                    invoke()
                result['database_cleanup_verified'] = all(c.get('cleanup_verified', True) for c in commands)
        if any(c.get('exit_code', 0) != 0 for c in commands):
            result['status'] = 'FAIL'
            result['reason'] = 'one or more owned commands exited unsuccessfully'
        if any(c.get('forced_kill') for c in commands):
            result['status'] = 'FAIL'
            result['reason'] = 'owned process needed forced termination; this is recovery, not clean shutdown'
    except FileNotFoundError as error:
        result.update(status='BLOCKED', reason=str(error))
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        result.update(status='FAIL', reason=str(error))
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--go', default='go')
    parser.add_argument('--pg-bin', type=Path)
    parser.add_argument('--source-sha', required=True, help='Expected current checkout HEAD; mismatch fails closed')
    parser.add_argument('--case', action='append', choices=CASES)
    parser.add_argument('--refresh-scenario', choices=REFRESH,
                        help='Rerun exactly one Windows refresh trial; default runs all four')
    args = parser.parse_args(argv)
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    head = subprocess.run(['git', 'rev-parse', 'HEAD'], cwd=SOURCE, check=True,
                          capture_output=True, text=True).stdout.strip()
    if head != args.source_sha:
        parser.error(f'checkout identity mismatch: expected {args.source_sha}, got {head}')
    results = []
    for case in args.case or CASES:
        evidence = args.output / case
        evidence.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='acceptance119-account-') as temporary:
            profile = Path(temporary)
            env = {k: v for k, v in os.environ.items()
                   if not k.startswith(('COLOSSEUM_', 'PG')) and k not in
                   ('TEST_DATABASE_URL', 'DATABASE_URL', 'MIGRATION_DATABASE_URL')}
            env.update(HOME=str(profile), XDG_DATA_HOME=str(profile / 'data'),
                       XDG_CONFIG_HOME=str(profile / 'config'), XDG_CACHE_HOME=str(profile / 'cache'),
                       COLOSSEUM_APPDATA_TAG='acceptance119-' + uuid.uuid4().hex,
                       QT_QPA_PLATFORM='offscreen')
            if os.name == 'nt':
                env.update(USERPROFILE=str(profile), APPDATA=str(profile / 'roaming'),
                           LOCALAPPDATA=str(profile / 'local'))
            result = run_case(case, args, env, profile, evidence)
            result.update(source_sha=head, os=platform.platform(),
                          appdata_tag=env['COLOSSEUM_APPDATA_TAG'],
                          network='Account-service traffic stays on loopback; Go may fetch public module dependencies.',
                          app_or_installer_sha256=None,
                          limits='Component evidence does not qualify assembled GUI, installer, or Windows helper cleanup.')
        result['profile_cleanup_verified'] = not profile.exists()
        if not result['profile_cleanup_verified']:
            result['status'] = 'FAIL'
        results.append(result)
        (evidence / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
        print(f"{case}: {result['status']}", flush=True)
    (args.output / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    return 1 if any(r['status'] == 'FAIL' for r in results) else 2 if any(r['status'] != 'PASS' for r in results) else 0


if __name__ == '__main__':
    raise SystemExit(main())
