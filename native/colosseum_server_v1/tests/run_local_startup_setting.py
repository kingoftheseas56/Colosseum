#!/usr/bin/env python3
"""Production local-addon manifest follows M564's startup settings snapshot.

All data is disposable. No roots, provider requests, peer discovery, or automatic
media scanning are enabled. Manifest expectations are the pinned M499/M1054
oracle fixtures already independently compared by LOCAL-differential.
"""
import argparse
import hashlib
import http.client
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True)
    parser.add_argument('--work', required=True, type=Path)
    args = parser.parse_args()
    rows = json.loads(Path(__file__).with_name('fixtures').joinpath('local-addon.json').read_text())
    manifests = {not row['disabled']: row['expected'] for row in rows if row['action'] == 'manifest'}
    work = Path(tempfile.mkdtemp(prefix='local-startup-setting-', dir=args.work)).resolve()
    cases = [('absent', None, False), ('false', False, False), ('true', True, True),
             ('null', None, False), ('zero', 0, False), ('empty-string', '', False),
             ('one', 1, True), ('string-false', 'false', True),
             ('empty-array', [], True), ('empty-object', {}, True)]
    checks = []
    for name, value, enabled in cases:
        case = work / name
        case.mkdir()
        home = case / 'home'
        home.mkdir()
        # A valid synthetic torrent under HOME must not become an implicit root.
        def bencode(value):
            if isinstance(value, int):
                return b'i' + str(value).encode() + b'e'
            if isinstance(value, bytes):
                return str(len(value)).encode() + b':' + value
            return b'd' + b''.join(bencode(key) + bencode(value[key])
                                   for key in sorted(value)) + b'e'
        info = {b'name': b'fixture.mp4', b'length': 1, b'piece length': 16384,
                b'pieces': hashlib.sha1(b'x').digest()}
        (home / 'fixture.torrent').write_bytes(bencode({b'info': info}))
        settings_path = case / 'server-settings.json'
        settings = {'transcodeHardwareAccel': False}
        if name != 'absent':
            settings['localAddonEnabled'] = value
        settings_path.write_text(json.dumps(settings))
        env = os.environ.copy()
        for key in ('APP_PATH', 'SETTINGS_PATH', 'HLS_DEBUG', 'IS_IOS', 'DISABLE_CACHING', 'SERVER_IPC_KEY'):
            env.pop(key, None)
        env.update(APP_PATH=str(case), SETTINGS_PATH=str(case),
                   HOME=str(home), XDG_CONFIG_HOME=str(case / 'config'),
                   XDG_CACHE_HOME=str(case / 'cache'))
        for phase in ('initial', 'restart'):
            log_path = case / (phase + '.log')
            with log_path.open('w') as log:
                process = subprocess.Popen([args.host, '--serve', '--port', '0',
                    '--cache', str(case / 'torrent-cache'), '--no-discovery',
                    '--fixed-interface', '--quit-after-ms', '15000'], env=env,
                    stdout=log, stderr=subprocess.STDOUT)
                try:
                    deadline = time.monotonic() + 8
                    port = None
                    while port is None:
                        output = log_path.read_text()
                        assert process.poll() is None and time.monotonic() < deadline, output
                        port = next((int(line.split()[1]) for line in output.splitlines()
                                     if line.startswith('LISTEN ')), None)
                        if port is None:
                            time.sleep(.01)

                    def request(path, body=None):
                        connection = http.client.HTTPConnection('127.0.0.1', port, timeout=3)
                        try:
                            connection.request('POST' if body is not None else 'GET', path,
                                json.dumps(body) if body is not None else None,
                                {'Content-Type': 'application/json'} if body is not None else {})
                            response = connection.getresponse()
                            payload = response.read()
                            assert response.status == 200, (name, phase, path, response.status, payload)
                            return json.loads(payload)
                        finally:
                            connection.close()

                    expected = enabled if phase == 'initial' else not enabled
                    before = request('/local-addon/manifest.json')
                    assert before == manifests[expected], (name, phase, before, manifests[expected])
                    assert request('/local-addon/catalog/other/local.json') == {'metas': []}
                    if phase == 'initial':
                        assert request('/settings')['values']['localAddonEnabled'] == (
                            False if name == 'absent' else value)
                        assert request('/settings', {'localAddonEnabled': not enabled}) == {'success': True}
                        assert request('/local-addon/manifest.json') == before, 'manifest changed without restart'
                        assert json.loads(settings_path.read_text())['localAddonEnabled'] is (not enabled)
                    checks.append({'case': name, 'phase': phase, 'catalog_advertised': expected})
                finally:
                    if process.poll() is None:
                        process.terminate()
                        try:
                            process.wait(timeout=5)
                        except subprocess.TimeoutExpired:
                            process.kill()
                            process.wait()
    (work / 'receipt.json').write_text(json.dumps(checks, indent=2) + '\n')
    print(f'PASS {len(checks)} production manifest cases, truthiness, persisted restart and no auto-indexing; {work}')


if __name__ == '__main__':
    main()
