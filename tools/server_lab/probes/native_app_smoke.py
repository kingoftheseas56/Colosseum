"""Verify the full application's opt-in native server with disposable user data.

Supply the platform's ordinary Qt/mpv library environment when needed. This
driver does not change certificate validation, browser sandboxing, or host
network permissions. It exercises lawful repository media, not public torrents.
"""
import argparse
import hashlib
import http.client
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import tempfile
import time
import urllib.parse


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--candidate', required=True, type=Path)
    parser.add_argument('--source', required=True, type=Path)
    parser.add_argument('--work', required=True, type=Path)
    parser.add_argument('--ffmpeg', default='ffmpeg')
    parser.add_argument('--offscreen', action='store_true')
    parser.add_argument('--expect-no-hardware', action='store_true',
                        help='Require the tested no-acceleration result on a known CPU-only host')
    args = parser.parse_args()
    source = args.source.resolve()
    candidate = args.candidate.resolve()
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='native-app-smoke-', dir=args.work))
    runtime = work / 'runtime'
    runtime.mkdir()
    profile = work / 'profile'
    for leaf in ('home', 'config', 'data', 'cache', 'runtime'):
        (profile / leaf).mkdir(parents=True)
    (profile / 'runtime').chmod(0o700)
    for name in ('qml', 'assets', 'data', 'resources', 'extensions', 'scripts'):
        if (source / name).exists():
            (runtime / name).symlink_to(source / name, target_is_directory=True)

    env = os.environ.copy()
    for key in tuple(env):
        if key.startswith('COLOSSEUM_') or key in ('APP_PATH', 'SETTINGS_PATH', 'SERVER_IPC_KEY'):
            env.pop(key)
    assert env.get('QTWEBENGINE_DISABLE_SANDBOX', '0') in ('', '0')
    assert not any(flag in env.get('QTWEBENGINE_CHROMIUM_FLAGS', '') for flag in (
        '--no-sandbox', '--ignore-certificate-errors', '--disable-web-security'))
    env.update(HOME=str(profile / 'home'), XDG_CONFIG_HOME=str(profile / 'config'),
               XDG_DATA_HOME=str(profile / 'data'), XDG_CACHE_HOME=str(profile / 'cache'),
               XDG_RUNTIME_DIR=str(profile / 'runtime'), COLOSSEUM_SERVER1='1',
               COLOSSEUM_APPDATA_TAG=work.name, COLOSSEUM_LANISTA_DRIVE='0',
               COLOSSEUM_LANISTA_WRITE='0', COLOSSEUM_LANISTA_PIPE=str(work / 'lanista'))
    if args.offscreen:
        env.update(QT_QPA_PLATFORM='offscreen', QT_QUICK_BACKEND='software')
    receipt = {'binary_sha256': hashlib.sha256(candidate.read_bytes()).hexdigest(),
               'offscreen': args.offscreen, 'result': 'pending'}
    with (work / 'app.log').open('w') as log:
        process = subprocess.Popen([str(candidate)], cwd=runtime, env=env,
                                   stdout=log, stderr=log, start_new_session=True)
        try:
            deadline = time.monotonic() + 35
            while True:
                output = (work / 'app.log').read_text()
                match = re.search(r'\[stream\] native Server 1 listening on port (\d+)', output)
                if match:
                    break
                assert process.poll() is None and time.monotonic() < deadline, output[-8000:]
                time.sleep(.1)
            port = int(match.group(1))
            receipt['port'] = port

            def get(path, expected_type=None):
                connection = http.client.HTTPConnection('127.0.0.1', port, timeout=15)
                try:
                    connection.request('GET', path)
                    response = connection.getresponse()
                    data = response.read()
                    assert response.status == 200, (path, response.status, data[:300])
                    if expected_type:
                        assert response.getheader('Content-Type') == expected_type, (
                            path, response.getheader('Content-Type'))
                    return data
                finally:
                    connection.close()

            assert json.loads(get('/heartbeat')) == {'success': True}
            assert json.loads(get('/casting')) == []
            local_manifest = json.loads(get('/local-addon/manifest.json'))
            assert local_manifest['id'] == 'org.stremio.local'
            assert local_manifest['catalogs'] == [] and 'catalog' not in local_manifest['resources']
            assert json.loads(get('/local-addon/catalog/other/local.json')) == {'metas': []}
            deadline = time.monotonic() + 5
            while True:
                settings = json.loads(get('/settings'))['values']
                if settings['btMaxConnections'] == 200:
                    break
                assert time.monotonic() < deadline, settings
                time.sleep(.1)
            assert settings['btDownloadSpeedSoftLimit'] == 20971520
            assert settings['btDownloadSpeedHardLimit'] == 41943040
            saved = list(profile.rglob('server-settings.json'))
            assert len(saved) == 1 and json.loads(saved[0].read_text())['btMaxConnections'] == 200
            media = source / 'artifacts/server1/M01/media/tiny.mp4'
            base = '/file/' + urllib.parse.quote(str(media), safe='') + '/'
            assert get(base + 'hls.m3u8').startswith(b'#EXTM3U')
            assert get(base + 'video0.m3u8').startswith(b'#EXTM3U')
            joined = get(base + 'video0/init.mp4') + get(base + 'video0/segment1.m4s')
            (work / 'video.mp4').write_bytes(joined)
            subprocess.run([args.ffmpeg, '-v', 'error', '-i', str(work / 'video.mp4'),
                            '-f', 'null', '-'], check=True, timeout=15)
            # First playback must work without waiting for device profiling to finish.
            device = json.loads(get('/device-info'))
            assert set(device) == {'availableHardwareAccelerations'}, device
            accelerations = device['availableHardwareAccelerations']
            assert accelerations is False or (
                isinstance(accelerations, list) and accelerations
                and all(isinstance(item, str) for item in accelerations)), device
            if args.expect_no_hardware:
                assert accelerations is False, device
            receipt['available_hardware_accelerations'] = accelerations
            manifest = json.loads((source / 'tools/server_lab/profiler_samples/manifest.json').read_text())
            sample_checks = []
            for sample in manifest['samples']:
                payload = get(sample['route'], sample['mime'])
                digest = hashlib.sha256(payload).hexdigest()
                assert len(payload) == sample['bytes'] and digest == sample['sha256'], sample['file']
                sample_checks.append({'route': sample['route'], 'bytes': len(payload), 'sha256': digest})
            receipt['generated_samples'] = sample_checks
            receipt.update(result='pass', checks=[
                'full application starts native server in-process', 'heartbeat',
                'passive casting and local addon routes without discovery',
                'device info returns verified profiles or false',
                'owned generated profiler samples served by default with exact hashes and MIME types',
                'tuned settings persisted', 'local media HLS master/playlist/init/segment',
                'FFmpeg decode'])
        except Exception as error:
            receipt.update(result='fail', error=str(error))
            raise
        finally:
            try:
                os.killpg(process.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
            (work / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
            print(work, receipt['result'])


if __name__ == '__main__':
    main()
