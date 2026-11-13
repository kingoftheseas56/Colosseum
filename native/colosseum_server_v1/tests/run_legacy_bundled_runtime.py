#!/usr/bin/env python3
"""Explicit real legacy runtime qualification with caller-selected executables.

No downloads and no public-network media. Pass the intended Stremio FFmpeg, not
an unrelated system version. Optional --ffsplit enables real splitter decoding;
without it the unavailable splitter response is checked and reported as a gap.
"""
import argparse
import hashlib
import http.client
import json
import os
import pathlib
import subprocess
import tempfile
import time
import urllib.parse

p = argparse.ArgumentParser()
p.add_argument('--host', required=True)
p.add_argument('--ffmpeg', required=True)
p.add_argument('--ffprobe', required=True)
p.add_argument('--ffsplit')
p.add_argument('--decoder', help='independent output decoder; defaults to --ffmpeg')
p.add_argument('--work', required=True)
a = p.parse_args()
work = pathlib.Path(tempfile.mkdtemp(prefix='legacy-bundled-', dir=a.work))
media = work / 'synthetic-h264-aac.mp4'
subprocess.run([a.ffmpeg, '-hide_banner', '-loglevel', 'error', '-y',
                '-f', 'lavfi', '-i', 'testsrc2=size=320x240:rate=25',
                '-f', 'lavfi', '-i', 'sine=frequency=440:sample_rate=48000',
                '-t', '7', '-c:v', 'libx264', '-g', '50', '-bf', '0',
                '-sc_threshold', '0', '-c:a', 'aac', str(media)], check=True)
records = []
env = dict(os.environ)
if a.ffsplit:
    env['FFSPLIT_BIN'] = str(pathlib.Path(a.ffsplit).resolve())
else:
    env.pop('FFSPLIT_BIN', None)
log = work / 'host.log'
with log.open('w') as output:
    host = subprocess.Popen([a.host, '--serve', '--port', '0', '--no-discovery',
        '--fixed-interface', '--disable-hlsv2', '--cache', str(work / 'cache'),
        '--ffmpeg', a.ffmpeg, '--ffprobe', a.ffprobe, '--quit-after-ms', '120000'],
        stdout=output, stderr=subprocess.STDOUT, env=env)
    try:
        deadline = time.monotonic() + 10
        port = None
        while time.monotonic() < deadline:
            assert host.poll() is None, log.read_text()
            port = next((int(line.split()[1]) for line in log.read_text().splitlines()
                         if line.startswith('LISTEN ')), None)
            if port:
                break
            time.sleep(.02)
        assert port, log.read_text()
        base = '/file/' + urllib.parse.quote(str(media), safe='')

        def fetch(suffix, name, expected=200):
            connection = http.client.HTTPConnection('127.0.0.1', port, timeout=30)
            try:
                connection.request('GET', base + suffix)
                response = connection.getresponse()
                body = response.read()
                headers = dict(response.getheaders())
                assert response.status == expected, (suffix, response.status, body[:500])
                dest = work / name
                dest.write_bytes(body)
                records.append(dict(path=suffix, status=response.status, bytes=len(body),
                    sha256=hashlib.sha256(body).hexdigest(), headers=headers))
                print('PASS HTTP', suffix, response.status, len(body), flush=True)
                return dest, headers
            finally:
                connection.close()

        def decode(path):
            info = json.loads(subprocess.check_output([a.ffprobe, '-v', 'error',
                '-show_streams', '-show_format', '-of', 'json', str(path)]))
            assert info.get('streams'), info
            subprocess.run([a.decoder or a.ffmpeg, '-v', 'error', '-i', str(path), '-f', 'null', '-'], check=True)
            records[-1]['streams'] = [dict(codec_type=s['codec_type'],codec_name=s['codec_name']) for s in info['streams']]
            records[-1]['decoded'] = True
            print('PASS decode', path.name, records[-1]['streams'], flush=True)

        master, _ = fetch('/master.m3u8', 'master.m3u8')
        assert b'stream-0.m3u8' in master.read_bytes()
        playlist, _ = fetch('/stream-q-240.m3u8', 'playlist.m3u8')
        assert b'#EXTINF:6.000' in playlist.read_bytes()
        segment, headers = fetch('/stream-q-240/0.ts', 'transcode.ts')
        assert headers.get('x-hls-flow') == 'transcoder'
        decode(segment)
        dlna, headers = fetch('/dlna', 'dlna.ts')
        assert headers.get('content-type') == 'video/vnd.dlna.mpeg-tts'
        assert headers.get('transfermode.dlna.org') == 'Streaming'
        decode(dlna)
        thumb, headers = fetch('/thumb.jpg', 'thumbnail.jpg')
        assert headers.get('content-type') == 'image/jpg' and thumb.read_bytes().startswith(b'\xff\xd8')
        decode(thumb)
        if a.ffsplit:
            for index in ('0', '1'):
                split, headers = fetch('/stream-' + index + '/0.ts', 'split-' + index + '.ts')
                assert headers.get('x-hls-flow') == 'splitter'
                decode(split)
        else:
            missing, _ = fetch('/stream-q-o/0.ts', 'missing-splitter.txt', 500)
            assert missing.read_bytes() == b'no ffsplit found'
            print('UNQUALIFIED real ffsplit: executable not supplied; absence response verified', flush=True)
    finally:
        if host.poll() is None:
            host.terminate()
        try:
            host.wait(timeout=10)
        except subprocess.TimeoutExpired:
            host.kill()
            host.wait(timeout=5)
receipt = dict(decoder_sha256=hashlib.sha256(pathlib.Path(a.decoder or a.ffmpeg).read_bytes()).hexdigest(),
               ffprobe_sha256=hashlib.sha256(pathlib.Path(a.ffprobe).read_bytes()).hexdigest(),
               ffmpeg_sha256=hashlib.sha256(pathlib.Path(a.ffmpeg).read_bytes()).hexdigest(),
               ffmpeg_version=subprocess.check_output([a.ffmpeg, '-version'], text=True).splitlines()[0],
               ffsplit_qualified=bool(a.ffsplit), requests=records)
(work / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
print('Evidence:', work)
