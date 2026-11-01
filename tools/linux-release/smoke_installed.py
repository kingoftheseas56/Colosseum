#!/usr/bin/env python3
"""Bounded installed-package check: native GTK picker, startup, natural exit,
and real production content over verified TLS. No provider or playback claims."""
import argparse
import json
import os
from pathlib import Path, PurePosixPath
import re
import socket
import subprocess
import tempfile
import time

CA_BUNDLE = Path('/etc/ssl/certs/ca-certificates.crt')
CATALOGUE_PREFIX = 'colosseum.catalogue.static: '
BANNER_OBJECT = 'homeUniverseBannerArt'
UI_DEADLINE_SECONDS = 90
BANNER_DEADLINE_SECONDS = 90
CATALOGUE_DEADLINE_SECONDS = 120
PICKER_DEADLINE_SECONDS = 15


def command(address, name, payload=None):
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
        connection.settimeout(5)
        connection.connect(str(address))
        connection.sendall(json.dumps({'seq': 1, 'cmd': name, 'payload': payload or {}}).encode() + b'\n')
        data = b''
        while b'\n' not in data:
            block = connection.recv(65536)
            if not block:
                raise RuntimeError('Lanista closed without a reply')
            data += block
        reply = json.loads(data.split(b'\n', 1)[0])
        if reply.get('type') != 'reply':
            raise RuntimeError(str(reply))
        return reply


def count_ssl_handshake_failures(text):
    return sum(1 for line in text.splitlines() if 'SSL handshake failed' in line)


def parse_catalogue_records(text):
    decoder = json.JSONDecoder()
    records = []
    for line in text.splitlines():
        index = line.find(CATALOGUE_PREFIX)
        if index < 0:
            continue
        rest = line[index + len(CATALOGUE_PREFIX):]
        brace = rest.find('{')
        if brace < 0:
            continue
        try:
            record, _ = decoder.raw_decode(rest[brace:])
        except ValueError:
            continue
        if isinstance(record, dict):
            records.append(record)
    return records


def content_bundle_records(records):
    return [record for record in records
            if record.get('dataset') == 'content' and record.get('method') == 'bundle']


def trust_snapshot():
    probe = subprocess.run(['dpkg-query', '-W', '-f=${db:Status-Abbrev}', 'ca-certificates'],
                           capture_output=True, text=True)
    return {'ca_certificates_status': probe.stdout.strip() or 'unknown',
            'ca_bundle_bytes': CA_BUNDLE.stat().st_size if CA_BUNDLE.is_file() else 0}


def gtk_runtime_evidence(pid):
    """Inspect the running packaged process, including AppRun's final environment."""
    proc = Path('/proc') / str(pid)
    environment = dict(entry.split('=', 1) for entry in
                       proc.joinpath('environ').read_bytes().decode().split('\0')
                       if '=' in entry)
    loader_dir = PurePosixPath('/opt/colosseum/usr/lib/gdk-pixbuf-loaders')
    module_dir = environment.get('GDK_PIXBUF_MODULEDIR', '')
    cache_file = environment.get('GDK_PIXBUF_MODULE_FILE', '')
    if module_dir != str(loader_dir) or not cache_file:
        raise RuntimeError(f'Packaged GdkPixbuf environment missing: {module_dir!r}, {cache_file!r}')
    modules = re.findall(r'^"(/[^"\n]+)"\s*$', Path(cache_file).read_text(), re.MULTILINE)
    if not modules or any(PurePosixPath(module).parent != loader_dir for module in modules):
        raise RuntimeError(f'Loader cache contains missing or nonpackaged modules: {modules}')
    if any(not Path(module).is_file() for module in modules):
        raise RuntimeError('Loader cache names a missing packaged module')
    if not any(re.search(r'[_-]svg\.so$', module) for module in modules):
        raise RuntimeError('Loader cache has no packaged SVG module')
    maps = proc.joinpath('maps').read_text()
    mapped = sorted(set(line.split(maxsplit=5)[5] for line in maps.splitlines()
                        if len(line.split(maxsplit=5)) == 6))
    theme = '/opt/colosseum/usr/plugins/platformthemes/libqgtk3.so'
    if theme not in mapped:
        raise RuntimeError('Forced gtk3 platform theme was not loaded from the package')
    svg_modules = [path for path in mapped if re.search(r'libpixbufloader[_-]svg\.so', path)]
    if any(PurePosixPath(module).parent != loader_dir for module in svg_modules):
        raise RuntimeError(f'Host SVG loader mapped into packaged process: {svg_modules}')
    return {'platform_theme': theme, 'module_dir': module_dir, 'cache_file': cache_file,
            'cache_modules': modules, 'mapped_svg_modules': svg_modules}


def xdotool(*arguments, allow_no_match=False):
    result = subprocess.run(['xdotool', *map(str, arguments)], capture_output=True,
                            text=True, timeout=5)
    if result.returncode and not (allow_no_match and result.returncode == 1):
        raise RuntimeError(f'xdotool {arguments!r} failed: {result.stderr.strip()}')
    return result.stdout.strip() if result.returncode == 0 else ''


def prove_gtk_picker(address, process, output, identity):
    proof = {'status': 'FAIL', 'source_commit': identity['source_commit'],
             'route': 'Ctrl+O', 'backend': 'native GTK',
             'opened': False, 'cancelled': False, 'alive_after_cancel': False}
    try:
        proof['runtime_before'] = gtk_runtime_evidence(process.pid)
        # GTK's modal chooser can nest/block the synchronous Lanista callback.
        # Deliver the real shortcut through XTest and keep IPC idle until closed.
        deadline = time.monotonic() + PICKER_DEADLINE_SECONDS
        main_window = None
        while time.monotonic() < deadline:
            if process.poll() is not None:
                raise RuntimeError(f'Application exited before GTK picker: {process.returncode}')
            matches = xdotool('search', '--all', '--onlyvisible', '--pid', process.pid,
                              '--name', '^Colosseum.*', allow_no_match=True).splitlines()
            for window in matches:
                title = xdotool('getwindowname', window)
                if not title.startswith('Colosseum'):
                    continue
                geometry = dict(line.split('=', 1) for line in
                                xdotool('getwindowgeometry', '--shell', window).splitlines()
                                if '=' in line)
                if int(geometry.get('WIDTH', 0)) > 0 and int(geometry.get('HEIGHT', 0)) > 0:
                    main_window = window
                    proof['main_window'] = {'window': window, 'title': title, 'geometry': geometry}
                    break
            if main_window:
                break
            time.sleep(0.2)
        else:
            raise RuntimeError('Visible Colosseum main window never appeared')
        xdotool('windowfocus', '--sync', main_window)
        xdotool('key', '--clearmodifiers', 'ctrl+o')
        deadline = time.monotonic() + PICKER_DEADLINE_SECONDS
        chooser = None
        while time.monotonic() < deadline:
            if process.poll() is not None:
                raise RuntimeError(f'Application exited opening GTK picker: {process.returncode}')
            matches = xdotool('search', '--all', '--onlyvisible', '--pid', process.pid,
                              '--name', '^Open Media$', allow_no_match=True).splitlines()
            for window in matches:
                if window == main_window:
                    continue
                title = xdotool('getwindowname', window)
                if title != 'Open Media':
                    continue
                geometry = dict(line.split('=', 1) for line in
                                xdotool('getwindowgeometry', '--shell', window).splitlines()
                                if '=' in line)
                if int(geometry.get('WIDTH', 0)) > 0 and int(geometry.get('HEIGHT', 0)) > 0:
                    chooser = window
                    proof['chooser'] = {'window': window, 'title': title, 'geometry': geometry}
                    break
            if chooser:
                proof['opened'] = True
                break
            time.sleep(0.2)
        else:
            raise RuntimeError('Native GTK Open Media chooser never appeared')
        proof['runtime_open'] = gtk_runtime_evidence(process.pid)
        # XSetInputFocus works without a window manager; XTest sends Escape to
        # the genuinely focused GTK window rather than Lanista's main window.
        xdotool('windowfocus', '--sync', chooser)
        xdotool('key', '--clearmodifiers', 'Escape')
        deadline = time.monotonic() + PICKER_DEADLINE_SECONDS
        while time.monotonic() < deadline:
            if process.poll() is not None:
                raise RuntimeError(f'Application exited cancelling GTK picker: {process.returncode}')
            visible = xdotool('search', '--all', '--onlyvisible', '--pid', process.pid,
                              '--name', '.*', allow_no_match=True).splitlines()
            if chooser not in visible:
                proof['cancelled'] = True
                break
            time.sleep(0.2)
        else:
            raise RuntimeError('Native GTK chooser did not close on Escape')
        xdotool('windowfocus', '--sync', main_window)
        state = command(address, 'get-state')
        if process.poll() is not None or not any(w.get('visible') for w in state.get('windows', [])):
            raise RuntimeError('Application unavailable after native picker cancellation')
        proof['alive_after_cancel'] = True
        proof['runtime_after'] = gtk_runtime_evidence(process.pid)
        proof['status'] = 'PASS'
        return proof
    except Exception as error:
        proof['error'] = str(error)
        raise
    finally:
        (output / 'installed-picker-smoke.json').write_text(json.dumps(proof, indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--platform-theme', choices=['gtk3'], default='gtk3',
                        help='Require the packaged GTK native chooser in the picker regression')
    args = parser.parse_args()
    if os.getuid() == 0:
        parser.error('Run the installed application as a normal user')
    args.output.mkdir(parents=True, exist_ok=True)
    version = subprocess.check_output(['/usr/bin/colosseum', '--version'], text=True).strip()
    identity = json.loads(Path('/opt/colosseum/BUILD-IDENTITY.json').read_text())
    if version != 'Colosseum ' + identity['version']:
        raise RuntimeError('Installed version differs from package identity')
    trust = trust_snapshot()
    if trust['ca_certificates_status'] != 'ii' or trust['ca_bundle_bytes'] <= 0:
        raise RuntimeError(f'System CA trust prerequisite absent on the installed host: {trust}')
    with tempfile.TemporaryDirectory(prefix='colosseum-installed-') as directory:
        profile = Path(directory)
        address = profile / 'lanista.sock'
        environment = os.environ.copy()
        # Test only packaged Qt, never the build job's SDK or a sandbox override.
        for key in list(environment):
            if key.startswith(('QT_', 'QML_', 'QTWEBENGINE', 'COLOSSEUM_', 'GDK_PIXBUF_')) or key in ('LD_LIBRARY_PATH', 'LD_PRELOAD', 'QTDIR'):
                environment.pop(key)
        environment.update({
            'XDG_CONFIG_HOME': str(profile / 'config'),
            'XDG_DATA_HOME': str(profile / 'data'),
            'XDG_CACHE_HOME': str(profile / 'cache'),
            'COLOSSEUM_LANISTA_PIPE': str(address),
            'COLOSSEUM_LANISTA_DRIVE': '1',
            # Diagnostic trace only: retains catalogue job reasons in the log.
            'COLOSSEUM_CATALOGUE_TRACE': '1',
            'QT_QPA_PLATFORM': 'xcb',
            'QT_FORCE_STDERR_LOGGING': '1',
            'LIBGL_ALWAYS_SOFTWARE': '1',
        })
        if args.platform_theme:
            environment['QT_QPA_PLATFORMTHEME'] = args.platform_theme
        log_path = args.output / 'installed-startup.log'
        with log_path.open('w') as log:
            process = subprocess.Popen(['/usr/bin/colosseum'], env=environment, stdout=log, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic() + UI_DEADLINE_SECONDS
                state = None
                last_error = None
                while time.monotonic() < deadline:
                    if process.poll() is not None:
                        raise RuntimeError(f'Application exited during startup: {process.returncode}')
                    try:
                        state = command(address, 'get-state')
                        splash = command(address, 'qml-get', {'object': 'bootSplash', 'props': ['visible']})
                        windows = state.get('windows', [])
                        if any(w.get('visible') and w.get('width', 0) > 0 and w.get('height', 0) > 0 for w in windows) and splash['props']['visible'] is False:
                            break
                    except (OSError, RuntimeError, KeyError) as error:
                        last_error = str(error)
                    time.sleep(1)
                else:
                    raise RuntimeError(f'Installed UI failed to become ready: {last_error}; {state}')
                if not Path(state['appDataRoot']).is_relative_to(profile / 'data'):
                    raise RuntimeError('Installed app escaped disposable data profile')
                onboarding = command(address, 'qml-get',
                                     {'object': 'accountHost', 'props': ['visible']})['props']
                if onboarding.get('visible') is True:
                    command(address, 'ui-click', {'target': 'accountWelcomeContinueLocal'})
                    deadline = time.monotonic() + PICKER_DEADLINE_SECONDS
                    while time.monotonic() < deadline:
                        onboarding = command(address, 'qml-get',
                                             {'object': 'accountHost', 'props': ['visible']})['props']
                        if onboarding.get('visible') is False:
                            break
                        time.sleep(0.2)
                    else:
                        raise RuntimeError('Local-only onboarding did not close')
                picker = prove_gtk_picker(address, process, args.output, identity)
                # A representative online image must become Ready through the
                # same normally verified HTTPS path the shelf artwork uses. The
                # banner delegate records readySource exactly when its status
                # reaches Image.Ready, so polling that string observes the
                # app's own readiness signal; a broken trust chain never sets
                # it and this wait fails.
                banner = {}
                banner_error = None
                deadline = time.monotonic() + BANNER_DEADLINE_SECONDS
                while time.monotonic() < deadline:
                    if process.poll() is not None:
                        raise RuntimeError(f'Application exited before its banner art: {process.returncode}')
                    try:
                        banner = command(address, 'qml-get',
                                          {'object': BANNER_OBJECT,
                                           'props': ['status', 'readySource', 'source']})['props']
                    except RuntimeError as error:
                        banner_error = str(error)
                    else:
                        banner_error = None
                    if isinstance(banner.get('readySource'), str) and banner['readySource'].startswith('https://'):
                        break
                    time.sleep(2)
                else:
                    raise RuntimeError(f'Online banner art never became Ready: {banner}; {banner_error}')
                banner_url = banner['readySource']
                # The native production catalogue must actually deliver the
                # content bundle over the network, not merely start a job.
                deadline = time.monotonic() + CATALOGUE_DEADLINE_SECONDS
                while time.monotonic() < deadline:
                    if process.poll() is not None:
                        raise RuntimeError(f'Application exited before its catalogue result: {process.returncode}')
                    if content_bundle_records(parse_catalogue_records(log_path.read_text(errors='replace'))):
                        break
                    time.sleep(2)
                else:
                    raise RuntimeError('Production content bundle catalogue result never appeared in the startup log')
                command(address, 'ui-keypress', {'key': 'Ctrl+Q'})
                process.wait(timeout=20)
                if process.returncode != 0:
                    raise RuntimeError(f'Application shutdown failed: {process.returncode}')
                log_text = log_path.read_text(errors='replace')
                ssl_errors = count_ssl_handshake_failures(log_text)
                records = parse_catalogue_records(log_text)
                failures = [record for record in records if record.get('outcome') == 'failure']
                bundles = content_bundle_records(records)
                final_bundle = bundles[-1] if bundles else None
                if ssl_errors:
                    raise RuntimeError(f'{ssl_errors} SSL handshake failures remain in the installed startup log')
                if failures:
                    raise RuntimeError(f'{len(failures)} catalogue records failed: '
                                       + json.dumps(failures[:3], sort_keys=True))
                if not final_bundle:
                    raise RuntimeError('Content bundle catalogue record missing after exit')
                if final_bundle.get('outcome') != 'prepared':
                    raise RuntimeError(f'Content bundle was not prepared: {final_bundle}')
                if int(final_bundle.get('networkBytes', 0)) <= 0:
                    raise RuntimeError(f'Content bundle delivered no network bytes: {final_bundle}')
                receipt = {'status': 'PASS', 'version': version, 'source_commit': identity['source_commit'],
                           'windows': state['windows'], 'exit_code': process.returncode,
                           'trust': trust, 'picker': picker,
                           'ssl_handshake_failures': ssl_errors,
                           'online': {
                               'banner_ready_url': banner_url,
                               'banner_status': banner.get('status'),
                               'catalogue_records': len(records),
                               'catalogue_failures': len(failures),
                               'content_bundle': {key: final_bundle.get(key) for key in
                                                  ('dataset', 'method', 'outcome', 'networkFiles',
                                                   'networkBytes', 'diskFiles', 'elapsedMs')}},
                           'scope': 'Installed native GTK picker open/cancel; X11 software-rendered startup '
                                    'and natural exit; production '
                                    'catalogue bundle and online banner verified over system-trusted TLS; '
                                    'playback, GPU and live providers unqualified'}
                (args.output / 'installed-smoke.json').write_text(json.dumps(receipt, indent=2) + '\n')
                print(json.dumps(receipt))
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()


if __name__ == '__main__':
    main()
