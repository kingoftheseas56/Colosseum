"""Run only a local, disposable-profile native probe; preserve JSONL evidence."""
import argparse
import functools
import hashlib
import http.server
import json
import os
from pathlib import Path
import subprocess
import tempfile
import threading
import time


class QuietHandler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *_):
        pass


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--negative-control', action='store_true')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    handler = functools.partial(QuietHandler, directory=str(Path(__file__).parent))
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    # Keep profiles outside the repository. Preserve them for diagnosis rather
    # than recursively deleting files while the browser process may still exit.
    profile = tempfile.mkdtemp(prefix='feria-wv2-close-')
    command = [str(args.exe.resolve()),
               f'http://127.0.0.1:{server.server_port}/fixture.html', profile]
    if args.negative_control:
        command.append('--force-close')
    started = time.time()
    try:
        completed = subprocess.run(command, capture_output=True, text=True,
                                   encoding='utf-8', timeout=90)
        stdout, stderr, code = completed.stdout, completed.stderr, completed.returncode
    except subprocess.TimeoutExpired as error:
        stdout = error.stdout or b''
        stderr = error.stderr or b''
        stdout = stdout.decode('utf-8') if isinstance(stdout, bytes) else stdout
        stderr = stderr.decode('utf-8') if isinstance(stderr, bytes) else stderr
        code = 124
    finally:
        server.shutdown()
        server.server_close()
    (args.output / 'events.jsonl').write_text(stdout, encoding='utf-8')
    (args.output / 'stderr.txt').write_text(stderr, encoding='utf-8')
    events = [json.loads(line) for line in stdout.splitlines() if line.strip()]
    summary = next((e for e in reversed(events) if e.get('event') == 'summary'), None)
    result = dict(scope='native-api-feasibility-not-Colosseum-host-parity',
                  executable_sha256=hashlib.sha256(args.exe.read_bytes()).hexdigest(),
                  loader_sha256=(hashlib.sha256((args.exe.parent / 'WebView2Loader.dll').read_bytes()).hexdigest()
                                 if (args.exe.parent / 'WebView2Loader.dll').exists() else None),
                  runtime=next((e['detail'] for e in events if e.get('event') == 'runtime'), None),
                  source_sha256={p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                                 for p in Path(__file__).parent.iterdir() if p.is_file()},
                  negative_control=args.negative_control, exit_code=code,
                  elapsed_seconds=round(time.time() - started, 3),
                  profile=profile, summary=summary,
                  environment_overrides={key: os.environ[key] for key in
                      ('WEBVIEW2_BROWSER_EXECUTABLE_FOLDER',
                       'WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS',
                       'WEBVIEW2_USER_DATA_FOLDER') if key in os.environ})
    (args.output / 'result.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result, indent=2))
    # RED must specifically reject the forced disposal, not merely crash/timeout.
    if args.negative_control:
        detected = any(e.get('event') == 'fail' and e.get('detail') ==
                       'negative control: forced Close bypasses required consent' for e in events)
        return 0 if code == 1 and summary and summary.get('negativeDetected') and detected else 1
    return 0 if code == 0 and summary and summary.get('failed') == 0 else 1


if __name__ == '__main__':
    raise SystemExit(main())
