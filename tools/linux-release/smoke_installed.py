#!/usr/bin/env python3
"""Bounded installed-package startup/exit check; no provider or playback claims."""
import argparse
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time


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


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    if os.getuid() == 0:
        parser.error('Run the installed application as a normal user')
    args.output.mkdir(parents=True, exist_ok=True)
    version = subprocess.check_output(['/usr/bin/colosseum', '--version'], text=True).strip()
    identity = json.loads(Path('/opt/colosseum/BUILD-IDENTITY.json').read_text())
    if version != 'Colosseum ' + identity['version']:
        raise RuntimeError('Installed version differs from package identity')
    with tempfile.TemporaryDirectory(prefix='colosseum-installed-') as directory:
        profile = Path(directory)
        address = profile / 'lanista.sock'
        environment = os.environ.copy()
        # Test only packaged Qt, never the build job's SDK or a sandbox override.
        for key in list(environment):
            if key.startswith(('QT_', 'QML_', 'QTWEBENGINE', 'COLOSSEUM_')) or key in ('LD_LIBRARY_PATH', 'LD_PRELOAD', 'QTDIR'):
                environment.pop(key)
        environment.update({
            'XDG_CONFIG_HOME': str(profile / 'config'),
            'XDG_DATA_HOME': str(profile / 'data'),
            'XDG_CACHE_HOME': str(profile / 'cache'),
            'COLOSSEUM_LANISTA_PIPE': str(address),
            'COLOSSEUM_LANISTA_DRIVE': '1',
            'QT_QPA_PLATFORM': 'xcb',
            'QT_FORCE_STDERR_LOGGING': '1',
            'LIBGL_ALWAYS_SOFTWARE': '1',
        })
        with (args.output / 'installed-startup.log').open('w') as log:
            process = subprocess.Popen(['/usr/bin/colosseum'], env=environment, stdout=log, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic() + 90
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
                command(address, 'ui-keypress', {'key': 'Ctrl+Q'})
                process.wait(timeout=20)
                if process.returncode != 0:
                    raise RuntimeError(f'Application shutdown failed: {process.returncode}')
                receipt = {'status': 'PASS', 'version': version, 'source_commit': identity['source_commit'],
                           'windows': state['windows'], 'exit_code': process.returncode,
                           'scope': 'Installed X11 software-rendered startup and natural exit; playback, GPU and live providers unqualified'}
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
