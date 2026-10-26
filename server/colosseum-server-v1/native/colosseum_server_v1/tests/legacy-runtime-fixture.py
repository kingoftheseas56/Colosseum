#!/usr/bin/python3
"""Deterministic legacy ffmpeg boundary; HTTP is restricted to loopback.

This is a process-lifecycle fixture, not a real media decoder. Each JSONL event
includes its PID so cancellation assertions establish OS-child termination.
"""
import json
import os
import sys
import time
import urllib.parse
import urllib.request

args = sys.argv[1:]
source = args[args.index('-i') + 1]
phase = 'probe' if args == ['-i', source] else 'convert'
log = os.environ.get('SERVER1_LEGACY_RUNTIME_LOG')


def event(kind, **details):
    if log:
        raw = (json.dumps(dict(event=kind, pid=os.getpid(), phase=phase,
                               source=source, **details)) + '\n').encode()
        fd = os.open(log, os.O_WRONLY | os.O_CREAT | os.O_APPEND, 0o600)
        try:
            os.write(fd, raw)
        finally:
            os.close(fd)


event('start')
if source.startswith('http://'):
    parsed = urllib.parse.urlsplit(source)
    if parsed.hostname not in ('127.0.0.1', '::1'):
        event('refused-non-loopback')
        sys.exit(91)
    # Disable environment proxies and reject redirects, including to non-loopback.
    class NoRedirect(urllib.request.HTTPRedirectHandler):
        def redirect_request(self, req, fp, code, msg, headers, newurl):
            return None
    try:
        with urllib.request.build_opener(urllib.request.ProxyHandler({}), NoRedirect()).open(source, timeout=2) as response:
            payload = response.read()
        event('fetch', bytes=len(payload))
    except Exception as error:
        event('fetch-error', error=str(error))
        sys.exit(92)

if phase == 'probe':
    if 'hang-probe' in source:
        while True:
            time.sleep(1)
    if 'invalid-probe' in source:
        sys.stderr.write('invalid fixture input\n')
        sys.stderr.flush()
        event('finish', code=0)
        sys.exit(0)
    sys.stderr.write("Input #0, avi, from 'fixture':\n"
                     "  Duration: 00:00:07.00, start: 0.000, bitrate: 1000 kb/s\n"
                     "    Stream #0:0: Video: h264, yuv420p, 640x480, 24 fps\n"
                     "    Stream #0:1: Audio: aac, 48000 Hz, stereo, 128 kb/s\n")
    sys.stderr.flush()
    event('finish', code=1)
    sys.exit(1)

if 'failed-convert' in source:
    event('finish', code=7)
    sys.exit(7)
os.write(1, b'\xff\xd8fixture-jpeg\xff\xd9' if '-vframes' in args else b'Gfixture-ts')
if 'queue-hold' in source or 'hang-convert' in source:
    while True:
        time.sleep(1)
event('finish', code=0)
