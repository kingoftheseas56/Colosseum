#!/usr/bin/env python3
"""Installed Main/QtWebEngine probe, real local-only profile restart and IPC evidence."""
import argparse
import http.server
import json
import os
from pathlib import Path
import subprocess
import tempfile
import threading
import time

from smoke_installed import command
from smoke_installed_player import clean_environment, installed_identity, sha256_file, stop_owned_process

MARKER = 'INSTALLED_RUNTIME_PROOF='
PAGE = b'<!doctype html><html><body style="background:#123;color:white;font:32px sans-serif"><h1 id="proof">Colosseum WebEngine qualification 130</h1></body></html>'


def validate_proof(record, phase):
    if record.get('phase') != phase or record.get('webResult') != 'Colosseum WebEngine qualification 130':
        raise RuntimeError('WebEngine did not return the expected DOM from the served page')
    if record.get('mode') != 'localOnly' or record.get('onboardingRequired') is not False:
        raise RuntimeError('Application local-only state missing')
    if phase == 'restart' and record.get('profileRetained') is not True:
        raise RuntimeError('Persisted local-only profile was not restored')


class PageHandler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200)
        self.send_header('Content-Type', 'text/html; charset=utf-8')
        self.send_header('Content-Length', str(len(PAGE)))
        self.end_headers()
        self.wfile.write(PAGE)

    def log_message(self, *_):
        pass


def run_phase(phase, profile, output, url, transcript, env):
    address = profile / 'runtime.sock'
    log_path = output / f'runtime-{phase}.log'
    home, web = output / f'home-{phase}.png', output / f'webengine-{phase}.png'
    driver = Path(__file__).with_name('InstalledRuntimeProof.qml').resolve()
    proc = None
    def ipc(name, payload=None):
        reply = command(address, name, payload)
        transcript.write(json.dumps({'phase': phase, 'request': {'cmd': name, 'payload': payload or {}}, 'reply': reply}) + '\n')
        transcript.flush()
        if reply.get('ok') is False:
            raise RuntimeError(f'IPC failed: {reply}')
        return reply
    try:
        with log_path.open('w') as log:
            proc = subprocess.Popen(['/usr/bin/colosseum', str(driver), phase, str(home), str(web), url], env=env, stdout=log, stderr=subprocess.STDOUT)
            deadline = time.monotonic() + 115
            state = None
            while time.monotonic() < deadline:
                if proc.poll() is not None:
                    raise RuntimeError(f'{phase} exited early: {proc.returncode}; see {log_path}')
                records = [json.loads(line.split(MARKER, 1)[1]) for line in log_path.read_text(errors='replace').splitlines() if MARKER in line]
                if records:
                    if len(records) != 1:
                        raise RuntimeError('Duplicate runtime proof')
                    validate_proof(records[0], phase)
                    state = ipc('get-state')
                    if not Path(state['appDataRoot']).is_relative_to(profile / 'data'):
                        raise RuntimeError('App data escaped isolated profile')
                    if not any(w.get('visible') and w.get('width', 0) > 0 and w.get('height', 0) > 0 for w in state.get('windows', [])):
                        raise RuntimeError('No visible application window')
                    break
                time.sleep(.25)
            else:
                raise TimeoutError(f'{phase} timed out')
            for frame in (home, web):
                if not frame.is_file() or frame.stat().st_size < 1024 or frame.read_bytes()[:8] != b'\x89PNG\r\n\x1a\n':
                    raise RuntimeError('Missing or invalid screenshot: ' + str(frame))
            ipc('qml-get', {'object': 'bootSplash', 'props': ['visible']})
            ipc('ui-keypress', {'key': 'Ctrl+Q'})
            proc.wait(timeout=20)
            if proc.returncode != 0:
                raise RuntimeError(f'{phase} did not quit normally: {proc.returncode}')
            return {'phase': phase, 'proof': records[0], 'appDataRoot': state['appDataRoot'], 'exit_code': proc.returncode,
                    'screenshots': {f.name: sha256_file(f) for f in (home, web)}}
    finally:
        stop_owned_process(proc)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    if os.getuid() == 0:
        ap.error('Run as a normal user')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    receipt = {'status': 'FAIL', 'scope': 'Installed Main QML with a separate test WebEngineView, software rendering, real persisted local-only onboarding; no live-provider, audio or GPU claim'}
    try:
        receipt['package_identity'] = installed_identity()
        with tempfile.TemporaryDirectory(prefix='colosseum-runtime-') as directory, \
                http.server.ThreadingHTTPServer(('127.0.0.1', 0), PageHandler) as server, \
                (output / 'runtime-lanista.jsonl').open('w') as transcript:
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            try:
                url = f'http://127.0.0.1:{server.server_port}/qualification'
                profile = Path(directory)
                env = clean_environment(profile)
                env.update(COLOSSEUM_LANISTA_PIPE=str(profile / 'runtime.sock'), COLOSSEUM_LANISTA_DRIVE='1')
                first = run_phase('first', profile, output, url, transcript, env)
                second = run_phase('restart', profile, output, url, transcript, env)
                if first['appDataRoot'] != second['appDataRoot']:
                    raise RuntimeError('Restart changed profile identity')
                receipt.update(status='PASS', launches=[first, second], page_url=url)
            finally:
                server.shutdown()
                thread.join(timeout=5)
    except Exception as error:
        receipt['error'] = str(error)
    (output / 'installed-runtime-proof.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps(receipt))
    return 0 if receipt['status'] == 'PASS' else 1


if __name__ == '__main__':
    raise SystemExit(main())
