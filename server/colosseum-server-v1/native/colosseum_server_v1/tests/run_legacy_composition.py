#!/usr/bin/env python3
"""Local production-composition smoke for M07 routing and process ownership.

This uses a deterministic executable fixture, not a media decoder. All HTTP goes
straight to 127.0.0.1. Discovery and interface monitoring are explicitly disabled.
"""
import argparse
import http.client
import json
import os
import pathlib
import socket
import subprocess
import tempfile
import time
import urllib.parse

parser = argparse.ArgumentParser()
parser.add_argument('--host', required=True)
parser.add_argument('--fixture', required=True)
parser.add_argument('--work', required=True)
parser.add_argument('--hlsv2-probe', default=str(pathlib.Path(__file__).with_name('hlsv2_probe_fixture.py')))
args = parser.parse_args()
work = pathlib.Path(tempfile.mkdtemp(prefix='legacy-composition-', dir=args.work))
fixture = str(pathlib.Path(args.fixture).resolve())
local_media = work / 'synthetic.avi'
local_media.write_bytes(b'M07 deterministic process fixture, not encoded media\n')
encoded = urllib.parse.quote(str(local_media), safe='')
raw_probe = {
    'format': {'format_name': 'avi', 'duration': '7', 'bit_rate': '1000000'},
    'streams': [
        {'index': 0, 'codec_type': 'video', 'codec_name': 'h264', 'width': 640, 'height': 480,
         'r_frame_rate': '24/1', 'time_base': '1/24000'},
        {'index': 1, 'codec_type': 'audio', 'codec_name': 'aac', 'sample_rate': '48000',
         'channels': 2, 'channel_layout': 'stereo', 'time_base': '1/48000'}]
}


def wait_for(ready, limit=5):
    deadline = time.monotonic() + limit
    while time.monotonic() < deadline:
        value = ready()
        if value:
            return value
        time.sleep(.01)
    raise AssertionError('deadline waiting for ' + getattr(ready, '__name__', 'condition'))


def process_dead(pid):
    try:
        os.kill(pid, 0)
        return False
    except ProcessLookupError:
        return True


def run_mode(enabled):
    name = 'hlsv2-enabled' if enabled else 'legacy-fallback'
    mode_work = work / name
    mode_work.mkdir()
    event_file = mode_work / 'events.jsonl'
    host_log = mode_work / 'host.log'
    env = dict(os.environ, SERVER1_LEGACY_RUNTIME_LOG=str(event_file),
               HLSV2_FIXTURE_RAW=json.dumps(raw_probe))
    env.pop('HLS_V2_DISABLED', None)
    command = [args.host, '--serve', '--port', '0', '--no-discovery', '--fixed-interface',
               '--cache', str(mode_work / 'cache'), '--ffmpeg', fixture,
               '--ffprobe', str(pathlib.Path(args.hlsv2_probe).resolve()), '--quit-after-ms', '45000']
    if not enabled:
        command.append('--disable-hlsv2')
    with host_log.open('w') as stream:
        host = subprocess.Popen(command, stdout=stream, stderr=subprocess.STDOUT, env=env)
        try:
            def listening():
                assert host.poll() is None, host_log.read_text()
                return next((int(line.split()[1]) for line in host_log.read_text().splitlines()
                             if line.startswith('LISTEN ')), None)
            port = wait_for(listening)

            def request(path, status=200, contains=None):
                connection = http.client.HTTPConnection('127.0.0.1', port, timeout=5)
                try:
                    connection.request('GET', path)
                    response = connection.getresponse()
                    body = response.read()
                    assert response.status == status, (path, response.status, body)
                    if contains is not None:
                        assert contains in body, (path, body)
                    return dict(response.getheaders()), body
                finally:
                    connection.close()

            def events():
                if not event_file.exists():
                    return []
                result = []
                for line in event_file.read_text().splitlines():
                    try:
                        result.append(json.loads(line))
                    except json.JSONDecodeError:
                        pass  # A concurrent append may not yet have completed.
                return result

            request('/stats.json', contains=b'{}')
            headers, body = request('/probe?url=%2Fstats.json', contains=b'duration')
            assert json.loads(body)['duration'] == 7000
            assert any(row['event'] == 'fetch' and row['source'] == f'http://127.0.0.1:{port}/stats.json'
                       for row in events()), 'relative /probe did not fetch current listener'
            base = '/file/' + encoded
            request(base + '/master.m3u8', contains=b'stream-0.m3u8')
            if enabled:
                _, body = request(base + '/hls.m3u8', contains=b'video0.m3u8')
                assert b'stream-q-' not in body, 'legacy route stole the HLSv2 alias'
            else:
                request(base + '/hls.m3u8', contains=b'stream-q-320.m3u8')
                request(base + '/stream-q-320.m3u8', contains=b'#EXTINF:6.000')
                request(base + '/stream-q-320/0.ts', contains=b'Gfixture-ts')
                request(base + '/stream-q-320/99.ts', status=404,
                        contains=b'segment does not exist in segmentsUniform')
                request(base + '/thumb.jpg', contains=b'fixture-jpeg')
                request('/thumb.jpg?from=' + encoded, contains=b'fixture-jpeg')
                request(base + '/dlna', contains=b'Gfixture-ts')
                request('/file/invalid-probe/master.m3u8', status=415,
                        contains=b'no video stream found')
                source = 'fixture://hang-probe-composition'
                with socket.create_connection(('127.0.0.1', port), timeout=5) as client:
                    target = '/probe?url=' + urllib.parse.quote(source, safe='')
                    client.sendall(('GET ' + target + ' HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n').encode())
                    def started():
                        return next((row['pid'] for row in events()
                                     if row['event'] == 'start' and row['source'] == source), None)
                    pid = wait_for(started)
                wait_for(lambda: process_dead(pid))
            print('PASS production composition ' + name + ': actual-port relative probe, '
                  + ('HLSv2 alias priority and independent legacy master' if enabled
                     else 'legacy aliases, playlists, media streams, errors, aborted-probe PID cleanup'), flush=True)
        finally:
            if host.poll() is None:
                host.terminate()
            try:
                host.wait(timeout=5)
            except subprocess.TimeoutExpired:
                host.kill()
                host.wait(timeout=5)


run_mode(False)
run_mode(True)
print('Composition evidence:', work)
