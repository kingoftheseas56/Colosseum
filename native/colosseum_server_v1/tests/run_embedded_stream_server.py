"""Real application StreamServer -> embedded composition -> local-media HLS."""
import argparse
import http.client
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time
import urllib.parse

parser = argparse.ArgumentParser()
parser.add_argument('--candidate', required=True)
parser.add_argument('--media', required=True)
parser.add_argument('--work', required=True)
args = parser.parse_args()
root = Path(tempfile.mkdtemp(prefix='embedded-server-', dir=args.work))
env = os.environ.copy()
env['XDG_CACHE_HOME'] = str(root / 'cache')
env['XDG_CONFIG_HOME'] = str(root / 'config')
env['HOME'] = str(root / 'home')
log = (root / 'host.log').open('w')
process = subprocess.Popen([args.candidate], stdout=log, stderr=subprocess.STDOUT, env=env)
try:
    deadline = time.monotonic() + 10
    while 'LISTEN ' not in (root / 'host.log').read_text():
        assert process.poll() is None and time.monotonic() < deadline, (root / 'host.log').read_text()
        time.sleep(.01)
    port = int(next(line.split()[1] for line in (root / 'host.log').read_text().splitlines()
                    if line.startswith('LISTEN ')))

    def request(path, method='GET', body=None):
        conn = http.client.HTTPConnection('127.0.0.1', port, timeout=8)
        conn.request(method, path, body, {'Content-Type': 'application/json'} if body else {})
        response = conn.getresponse()
        data = response.read()
        assert response.status == 200, (path, response.status, data)
        conn.close()
        return data

    assert json.loads(request('/heartbeat')) == {'success': True}
    assert json.loads(request('/casting')) == []
    assert json.loads(request('/local-addon/manifest.json'))['id'] == 'org.stremio.local'
    assert json.loads(request('/local-addon/catalog/other/local.json')) == {'metas': []}
    settings = json.loads(request('/settings'))
    assert settings['values']['btMaxConnections'] == 200, settings
    assert settings['values']['btDownloadSpeedSoftLimit'] == 20971520, settings
    base = '/file/' + urllib.parse.quote(str(Path(args.media).resolve()), safe='') + '/'
    master = request(base + 'hls.m3u8')
    assert master.startswith(b'#EXTM3U')
    assert request(base + 'video0.m3u8').startswith(b'#EXTM3U')
    init = request(base + 'video0/init.mp4')
    segment = request(base + 'video0/segment1.m4s')
    (root / 'media.mp4').write_bytes(init + segment)
    subprocess.run(['ffmpeg', '-v', 'error', '-i', str(root / 'media.mp4'), '-f', 'null', '-'],
                   check=True, timeout=8)
    assert process.wait(timeout=20) == 0, (root / 'host.log').read_text()
    assert 'STOPPED' in (root / 'host.log').read_text()
    print('PASS real StreamServer embedded startup/idempotency/settings/local HLS/decode/shutdown;', root)
finally:
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
    log.close()
