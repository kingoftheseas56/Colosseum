"""Exercise production browser wrappers with two local origins, without accounts."""
import http.server
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading


class Fixture(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_GET(self):
        if self.path == '/fixture/redirect':
            self.send_response(302)
            self.send_header('Location', f'http://localhost:{self.server.server_port}/fixture/page')
            self.end_headers()
            return
        passed = True
        script = ''
        if self.path == '/fixture/setcookie':
            self.send_response(200)
            self.send_header('Set-Cookie', 'feria_fixture=persisted; Max-Age=3600; Path=/')
        else:
            self.send_response(200)
        if self.path == '/fixture/checkcookie':
            passed = 'feria_fixture=persisted' in self.headers.get('Cookie', '')
        elif self.path == '/fixture/popup':
            passed = False
            script = f"""window.addEventListener('message', e => {{
                if(e.origin === 'http://localhost:{self.server.server_port}' && e.data === 'auth-returned')
                    document.body.dataset.passed='true'; }});
                window.open('http://localhost:{self.server.server_port}/fixture/auth', '_blank');"""
        elif self.path == '/fixture/auth':
            script = f"window.opener.postMessage('auth-returned', 'http://127.0.0.1:{self.server.server_port}'); window.close();"
        self.send_header('Content-Type', 'text/html; charset=utf-8')
        self.end_headers()
        body = f"<html><body data-passed='{str(passed).lower()}'>Feria browser fixture<script>{script}</script></body></html>"
        self.wfile.write(body.encode())


def main():
    repository = Path(__file__).resolve().parents[2]
    executable = repository / 'native/build-msvc/tests/feria_host_probe/feria_browser_harness.exe'
    if len(sys.argv) > 1:
        executable = Path(sys.argv[1]).resolve()
    environment = os.environ.copy()
    cache = repository / 'native/build-msvc/CMakeCache.txt'
    if cache.exists():
        for line in cache.read_text(encoding='utf-8').splitlines():
            if line.startswith('Qt6_DIR:PATH='):
                qt_bin = Path(line.split('=', 1)[1]).parents[2] / 'bin'
                environment['PATH'] = str(qt_bin) + os.pathsep + environment.get('PATH', '')
                break
    fixture = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Fixture)
    threading.Thread(target=fixture.serve_forever, daemon=True).start()
    with tempfile.TemporaryDirectory(prefix='feria-browser-test-') as directory:
        root = Path(directory)
        urls = root / 'urls.json'
        base = f'http://127.0.0.1:{fixture.server_port}/fixture/'
        failures = 0
        for engine in ['webview2', 'qtwebengine']:
            urls.write_text(json.dumps([base + route for route in
                ['page', 'redirect', 'setcookie', 'checkcookie', 'popup']]), encoding='utf-8')
            print(f'Testing {engine}: page, cross-domain redirect, cookies, popup/opener', flush=True)
            result = subprocess.run([str(executable), str(Path(__file__).with_name('BrowserSmoke.qml').resolve()),
                engine, str(urls), str(root / 'profiles')], timeout=150, env=environment)
            failures += result.returncode != 0
            urls.write_text(json.dumps([base + 'checkcookie']), encoding='utf-8')
            print(f'Testing {engine}: persistent cookies after process restart', flush=True)
            result = subprocess.run([str(executable), str(Path(__file__).with_name('BrowserSmoke.qml').resolve()),
                engine, str(urls), str(root / 'profiles')], timeout=45, env=environment)
            failures += result.returncode != 0
        fixture.shutdown()
        return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
