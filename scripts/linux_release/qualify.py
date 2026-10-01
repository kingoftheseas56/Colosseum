#!/usr/bin/env python3
"""Run inside a clean Ubuntu 24.04 container. Never convert unavailable into PASS."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import signal
import socket
import subprocess
import time
import uuid


# Ubuntu supplies glibc and the ELF interpreter. Everything else directly linked
# must resolve inside the extracted package, including Qt and libmpv.
HOST_ABI = re.compile(r'^(?:libc|libm|libpthread|libdl|librt|libresolv|libutil|libanl)\.so\.[0-9]+$|^ld-linux-x86-64\.so\.2$')


def required_tools(directory=None):
    tools = {}
    for name in ['ffmpeg', 'ffprobe', 'bsdtar']:
        candidate = directory / name if directory is not None else shutil.which(name)
        if not candidate or not Path(candidate).is_file() or not os.access(candidate, os.X_OK):
            raise RuntimeError('required runtime executable missing or non-executable: ' + name)
        tools[name] = Path(candidate)
    return tools


def loader_errors(output, appdir):
    errors = []
    for line in output.splitlines():
        if 'not found' in line:
            errors.append(line.strip())
        match = re.search(r'^\s*(\S+)\s+=>\s+(/\S+)', line)
        if match:
            name, location = match.groups()
            if not Path(location).resolve().is_relative_to(appdir.resolve()) and not HOST_ABI.fullmatch(name):
                errors.append('external dependency: ' + line.strip())
    return errors


def catalog_ready(log):
    return any(int(n) > 0 for n in re.findall(r'\[catalog-selftest\] row .*?\s(\d+) items', log))


def qml_errors(log):
    return re.findall(r'^.*(?:QQmlApplicationEngine failed|module ["\'].+["\'] is not installed|Type \S+ unavailable|ReferenceError:|TypeError:|SyntaxError:|startup layout rejected|Cannot load library|Failed to load platform plugin).*$', log, re.M)


def is_elf(path):
    """Select runtime ELF images (ET_EXEC/ET_DYN), not SDK ET_REL objects."""
    if not path.is_file():
        return False
    with path.open('rb') as stream:
        header = stream.read(18)
    # e_type follows the 16-byte identification in both ELF32 and ELF64.
    if (len(header) < 18 or header[:4] != b'\x7fELF'
            or header[4] not in (1, 2) or header[5] not in (1, 2)):
        return False
    byteorder = 'little' if header[5] == 1 else 'big'
    return int.from_bytes(header[16:18], byteorder) in (2, 3)


def closure(appdir, evidence, env):
    failures = []
    with (evidence / 'elf-closure.log').open('w') as log:
        for path in sorted(appdir.rglob('*')):
            if path.is_symlink() or not is_elf(path):
                continue
            log.write(str(path.relative_to(appdir)) + '\n')
            log.flush()  # Keep the blocked path even if ldd or the job times out.
            result = subprocess.run(['ldd', str(path)], env=env, text=True, capture_output=True, timeout=30)
            log.write(result.stdout + result.stderr)
            log.flush()
            failures.extend(loader_errors(result.stdout + result.stderr, appdir))
            if result.returncode and 'statically linked' not in result.stdout + result.stderr:
                failures.append('ldd failed: ' + str(path))
    if failures:
        raise RuntimeError('\n'.join(failures))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--appdir', type=Path, required=True)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--fixture', type=Path, required=True)
    args = parser.parse_args()
    appdir, evidence = args.appdir.resolve(), args.evidence.resolve()
    evidence.mkdir(parents=True, exist_ok=True)
    statuses = {key: 'NOT_RUN' for key in ['setup', 'elf_closure', 'launch', 'qml', 'catalog', 'playback']}
    report = {'checks': statuses, 'qualified': False, 'scope': 'Ubuntu 24.04 x86_64; Xvfb/Mesa software OpenGL',
              'limitations': ['No hardware GPU or audible-output qualification',
                              'Local fixture playback only; no remote stream, DRM, DVR or Stremio service qualification',
                              'Catalog check is a real live movie-catalog production request; not visual catalog UI, offline data or all providers',
                              'WebEngine assets are bundled; reader runtime is unverified',
                              'Direct PID-bound Lanista checks; not a Harness completionReady receipt']}
    def checkpoint(phase):
        report['phase'] = phase
        report['updated_at'] = time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())
        temporary = evidence / 'qualification.json.tmp'
        temporary.write_text(json.dumps(report, indent=2) + '\n')
        temporary.replace(evidence / 'qualification.json')
        print(report['updated_at'] + ' qualifier: ' + phase, flush=True)

    def interrupted(signum, frame):
        raise RuntimeError('qualification interrupted by signal ' + str(signum))

    signal.signal(signal.SIGTERM, interrupted)
    process = None
    active = 'setup'
    checkpoint(active)  # Persist before profile, DISPLAY or any subprocess setup.
    sequence = 0
    def call(cmd, payload=None):
        nonlocal sequence
        sequence += 1
        request = {'cmd': cmd, 'seq': sequence, 'payload': payload or {}}
        with socket.socket(socket.AF_UNIX) as sock:
            sock.settimeout(75)
            sock.connect(env['COLOSSEUM_LANISTA_PIPE'])
            sock.sendall(json.dumps(request).encode() + b'\n')
            buf = b''
            while b'\n' not in buf:
                chunk = sock.recv(65536)
                if not chunk:
                    raise RuntimeError('bridge closed without reply')
                buf += chunk
                if len(buf) > 4 * 1024 * 1024:
                    raise RuntimeError('oversized bridge response')
        reply = json.loads(buf.split(b'\n')[0])
        with (evidence / 'bridge.jsonl').open('a') as stream:
            stream.write(json.dumps({'request': request, 'reply': reply}) + '\n')
        if reply.get('type') != 'reply' or reply.get('seq') != sequence:
            raise RuntimeError('bridge error: ' + str(reply))
        return reply

    def wait_prop(obj, prop, value, timeout=15000):
        reply = call('ui-wait-for', {'object': obj, 'prop': prop, 'value': value, 'timeout_ms': timeout})
        if reply.get('matched') is not True:
            raise RuntimeError('property not reached: ' + str(reply))

    try:
        tag = 'linux-' + uuid.uuid4().hex[:12]
        profile = evidence / 'profile'
        profile.mkdir()
        env = {'PATH': '/usr/bin:/bin', 'HOME': str(profile), 'LANG': 'C.UTF-8',
               'DISPLAY': os.environ['DISPLAY'], 'XAUTHORITY': os.environ.get('XAUTHORITY', ''),
               'XDG_DATA_HOME': str(profile / 'data'), 'XDG_CONFIG_HOME': str(profile / 'config'),
               'XDG_CACHE_HOME': str(profile / 'cache'), 'XDG_RUNTIME_DIR': str(profile / 'run'),
               'LD_LIBRARY_PATH': str(appdir / 'usr/lib'), 'LIBGL_ALWAYS_SOFTWARE': '1',
               'QT_QPA_PLATFORM': 'xcb', 'QT_FORCE_STDERR_LOGGING': '1',
               'COLOSSEUM_APPDATA_TAG': tag, 'COLOSSEUM_LANISTA_DRIVE': '1',
               'COLOSSEUM_LANISTA_PIPE': str(profile / 'bridge.sock'),
               'COLOSSEUM_CATALOG_SELFTEST': 'movies'}
        Path(env['XDG_RUNTIME_DIR']).mkdir(mode=0o700)
        # Seed only the production Recent route, rewriting the Windows-only fixture
        # path to a generated Linux fixture. Catalog data is deliberately not seeded.
        data = Path(env['XDG_DATA_HOME']) / 'Brotherhood' / ('Colosseum-dltest-' + tag)
        (data / 'vault').mkdir(parents=True)
        (data / 'vault/open-recent.json').write_text(json.dumps({'items': [{
            'path': str(args.fixture.resolve()), 'title': 'Linux package playback fixture',
            'kind': 'video', 'vaultId': 'vault:linux-package-fixture'}]}))
        statuses[active] = 'PASS'
        active = 'elf_closure'
        checkpoint(active)
        required_tools(appdir / 'usr/bin')
        closure(appdir, evidence, env)
        statuses[active] = 'PASS'
        active = 'launch'
        checkpoint(active)
        with (evidence / 'app.log').open('w') as log:
            # AppRun execs the binary, preserving PID. No QML argument: the
            # production manifest fingerprint and relative resource layout apply.
            process = subprocess.Popen([str(appdir / 'AppRun')], cwd=profile, env=env, stdout=log, stderr=log)
            deadline = time.monotonic() + 120
            while not Path(env['COLOSSEUM_LANISTA_PIPE']).exists():
                if process.poll() is not None or time.monotonic() > deadline:
                    raise RuntimeError('app failed to expose its bridge; see app.log')
                time.sleep(0.2)
            pong = call('ping')
            if pong.get('pid') != process.pid:
                raise RuntimeError('bridge PID mismatch')
            state = call('get-state')
            if not any(w.get('visible') for w in state.get('windows', [])):
                raise RuntimeError('no visible application window')
            if Path(state['appDataRoot']).resolve() != data.resolve():
                raise RuntimeError('unexpected profile root; no isolation proof')
            statuses[active] = 'PASS'
            active = 'qml'
            checkpoint(active)
            wait_prop('bootSplash', 'visible', False, 60000)
            call('ui-keypress', {'key': 'Enter'})
            wait_prop('accountHost', 'visible', False)
            if qml_errors((evidence / 'app.log').read_text(errors='replace')):
                raise RuntimeError('QML load errors; see app.log')
            statuses[active] = 'PASS'
            active = 'playback'
            checkpoint(active)
            # Same UI entry point as the tagged journey_play_video scenario.
            call('ui-click', {'target': 'colosseumTaskbarHomeButton'})
            wait_prop('taskbarOpenMedia', 'width', 46, 5000)
            call('ui-click', {'target': 'openRecentDisclosure'})
            wait_prop('openRecentPanel', 'rowCount', 1)
            wait_prop('openRecentPanel', 'visible', True)
            call('ui-click', {'target': 'openRecentRow_0'})
            wait_prop('player', 'playerActive', True, 20000)
            wait_prop('player', 'decodedWidth', 64, 20000)
            wait_prop('player', 'decodedHeight', 64, 20000)
            frame = call('qml-get', {'object': 'player', 'props': ['decodedWidth', 'decodedHeight', 'playerReady', 'sourceIdentity'],
                                     'grab': {'target': 'window', 'timeoutMs': 4000}})
            props = frame.get('props', {})
            if props.get('playerReady') is not True or props.get('decodedWidth') != 64 or props.get('decodedHeight') != 64:
                raise RuntimeError('no decoded-frame readiness')
            if not str(props.get('sourceIdentity', '')).startswith('vault:'):
                raise RuntimeError('unexpected playback source identity')
            screenshot = Path(frame.get('grabPath', ''))
            if not screenshot.is_file():
                raise RuntimeError('PID-bound screenshot missing')
            shutil.copy2(screenshot, evidence / 'playback.png')
            statuses[active] = 'PASS'
            active = 'catalog'
            checkpoint(active)
            deadline = time.monotonic() + 180
            while not catalog_ready((evidence / 'app.log').read_text(errors='replace')):
                if process.poll() is not None or time.monotonic() > deadline:
                    raise RuntimeError('nonempty live catalog not observed; inspect app.log for network/provider failures')
                time.sleep(0.5)
            statuses[active] = 'PASS'
            if qml_errors((evidence / 'app.log').read_text(errors='replace')):
                statuses['qml'] = 'FAIL'
                raise RuntimeError('QML errors during runtime checks')
            report['qualified'] = all(value == 'PASS' for value in statuses.values())
    except Exception as error:
        if statuses[active] != 'PASS':
            statuses[active] = 'FAIL'
            if active == 'catalog' and re.search(r'manifest fetch failed|Host .*not found|Network is unreachable|SSL handshake failed',
                                                (evidence / 'app.log').read_text(errors='replace')):
                statuses[active] = 'UNAVAILABLE'
        report['error'] = str(error)
    finally:
        if process is not None and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        checkpoint('complete' if report['qualified'] else 'failed')
        print(json.dumps(report, indent=2))
    return 0 if report['qualified'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
