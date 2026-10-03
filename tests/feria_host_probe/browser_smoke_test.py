"""Exercise production browser wrappers with two local origins, without accounts."""
import http.server
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import io
import wave

audio_buffer = io.BytesIO()
with wave.open(audio_buffer, 'wb') as audio:
    audio.setnchannels(1)
    audio.setsampwidth(2)
    audio.setframerate(8000)
    audio.writeframes(b'\0\0' * 8000 * 40)
AUDIO_FIXTURE = audio_buffer.getvalue()


class Fixture(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_GET(self):
        if self.path == '/fixture/sw.js':
            self.send_response(200)
            self.send_header('Content-Type', 'text/javascript')
            self.end_headers()
            self.wfile.write(b"self.addEventListener('install',()=>self.skipWaiting());")
            return
        if self.path == '/fixture/audio.wav':
            start, end = 0, len(AUDIO_FIXTURE) - 1
            requested = self.headers.get('Range', '')
            if requested.startswith('bytes='):
                low, high = requested[6:].split('-', 1)
                start = int(low or 0)
                end = min(int(high) if high else end, end)
            self.send_response(206 if requested else 200)
            self.send_header('Content-Type', 'audio/wav')
            self.send_header('Accept-Ranges', 'bytes')
            if requested:
                self.send_header('Content-Range', f'bytes {start}-{end}/{len(AUDIO_FIXTURE)}')
            self.send_header('Content-Length', str(end - start + 1))
            self.end_headers()
            self.wfile.write(AUDIO_FIXTURE[start:end + 1])
            return
        if self.path == '/fixture/redirect':
            self.send_response(302)
            self.send_header('Location', f'http://localhost:{self.server.server_port}/fixture/page')
            self.end_headers()
            return
        passed = True
        script = ''
        if self.path in ['/fixture/setcookie', '/fixture/session-set']:
            self.send_response(200)
            self.send_header('Set-Cookie', 'feria_fixture=persisted; HttpOnly; Max-Age=3600; Path=/')
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
        if self.path == '/fixture/session-set':
            passed = False
            script = """(async()=>{document.body.dataset.stage='local';localStorage.setItem('feria_session','fixture');
                document.body.dataset.stage='database';
                await new Promise((ok,no)=>{const r=indexedDB.open('feria_session',1);r.onsuccess=()=>{r.result.close();ok()};r.onerror=no});
                document.body.dataset.stage='cache';
                await caches.open('feria_session');
                document.body.dataset.stage='worker';
                await navigator.serviceWorker.register('/fixture/sw.js');
                await navigator.serviceWorker.ready;
                document.body.dataset.stage='done';document.body.dataset.passed='true';})().catch(e=>document.body.dataset.detail=String(e))"""
        elif self.path.split('?')[0] in ['/fixture/session-cleared', '/fixture/session-kept']:
            cookie = 'feria_fixture=persisted' in self.headers.get('Cookie','')
            kept = self.path.endswith('kept')
            passed = False
            script = f"""(async()=>{{ const kept={str(kept).lower()}, cookie={str(cookie).lower()};
                const values=[cookie,localStorage.getItem('feria_session')==='fixture',
                    (await indexedDB.databases()).some(x=>x.name==='feria_session'),
                    (await caches.keys()).includes('feria_session')];
                if (kept || location.search !== '?qt-cleanup') values.push((await navigator.serviceWorker.getRegistrations()).length>0);
                document.body.dataset.detail=JSON.stringify(values);
                document.body.dataset.passed=String(values.every(value=>value===kept)); }})()"""
        if self.path == '/fixture/media-shadow':
            script = "document.body.attachShadow({mode:'open'}).innerHTML='<audio autoplay muted src=\"/fixture/audio.wav\"></audio>';"
        if self.path == '/fixture/app-mode':
            self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.end_headers()
            self.wfile.write(b'''<html><head><title>Feria app fixture</title></head><body>
                <main><button id="first" style="position:absolute;left:40px;top:80px;width:100px;height:60px">First</button>
                <button id="second" style="position:absolute;left:200px;top:80px;width:100px;height:60px">Second</button>
                <input id="typing" style="position:absolute;left:40px;top:200px" /></main><footer>Site footer</footer>
                <footer id="consent"><button>Accept cookies</button></footer></body></html>''')
            return
        self.send_header('Content-Type', 'text/html; charset=utf-8')
        self.end_headers()
        media = '<audio autoplay muted src="/fixture/audio.wav"></audio>' if self.path == '/fixture/media' else ''
        if self.path == '/fixture/media-frame':
            media = '<iframe src="/fixture/media"></iframe>'
        if '/fixture/reader/' in self.path:
            media = '<main id=reader data-reader style="height:350px;overflow:auto"><article style="height:2400px">Chapter fixture</article></main>'
        body = f"<html><head><title>Feria fixture</title></head><body data-passed='{str(passed).lower()}'>Feria browser fixture{media}<script>{script}</script></body></html>"
        self.wfile.write(body.encode())


def main():
    repository = Path(__file__).resolve().parents[2]
    executable = repository / 'native/build-msvc/tests/feria_host_probe/feria_browser_harness.exe'
    if len(sys.argv) > 1:
        executable = Path(sys.argv[1]).resolve()
    environment = os.environ.copy()
    environment['QT_FORCE_STDERR_LOGGING'] = '1'
    environment['QT_QUICK_CONTROLS_STYLE'] = 'Basic'
    cache = repository / 'native/build-msvc/CMakeCache.txt'
    if cache.exists():
        for line in cache.read_text(encoding='utf-8').splitlines():
            if line.startswith('Qt6_DIR:PATH='):
                qt_bin = Path(line.split('=', 1)[1]).parents[2] / 'bin'
                environment['PATH'] = str(qt_bin) + os.pathsep + environment.get('PATH', '')
                break
    fixture = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Fixture)
    threading.Thread(target=fixture.serve_forever, daemon=True).start()
    with tempfile.TemporaryDirectory(prefix='feria-browser-test-', ignore_cleanup_errors=True) as directory:
        root = Path(directory)
        urls = root / 'urls.json'
        base = f'http://127.0.0.1:{fixture.server_port}/fixture/'
        failures = 0
        urls.write_text(json.dumps(['feria-navigation']), encoding='utf-8')
        print('Testing Feria tabs and section sidebar', flush=True)
        result = subprocess.run([str(executable), str(Path(__file__).with_name('FeriaNavigationSmoke.qml').resolve()),
            'navigation', str(urls), str(root / 'navigation')], timeout=30, env=environment)
        failures += result.returncode != 0
        urls.write_text(json.dumps(['feria-chrome']), encoding='utf-8')
        print('Testing Feria navigation and window controls', flush=True)
        result = subprocess.run([str(executable), str(Path(__file__).with_name('FeriaChromeSmoke.qml').resolve()),
            'navigation', str(urls), str(root / 'chrome')], timeout=30, env=environment)
        failures += result.returncode != 0
        urls.write_text(json.dumps(['feria-see-all']), encoding='utf-8')
        print('Testing Feria row expansion, return state and Continue', flush=True)
        result = subprocess.run([str(executable), str(Path(__file__).with_name('FeriaSeeAllSmoke.qml').resolve()),
            'navigation', str(urls), str(root / 'see-all')], timeout=35, env=environment)
        failures += result.returncode != 0
        for engine in ['webview2', 'qtwebengine']:
            urls.write_text(json.dumps([base + 'app-mode']), encoding='utf-8')
            print(f'Testing {engine}: app layout and spatial navigation', flush=True)
            result = subprocess.run([str(executable), str(Path(__file__).with_name('FeriaAppModeSmoke.qml').resolve()),
                engine, str(urls), str(root / 'profiles')], timeout=45, env=environment)
            failures += result.returncode != 0
            urls.write_text(json.dumps([base + route for route in
                ['page', 'redirect', 'setcookie', 'checkcookie', 'popup', 'media', 'media-shadow', 'media-frame']]), encoding='utf-8')
            print(f'Testing {engine}: page, cross-domain redirect, cookies, popup/opener', flush=True)
            result = subprocess.run([str(executable), str(Path(__file__).with_name('BrowserSmoke.qml').resolve()),
                engine, str(urls), str(root / 'profiles')], timeout=150, env=environment)
            failures += result.returncode != 0
            urls.write_text(json.dumps([base + 'media']), encoding='utf-8')
            print(f'Testing {engine}: production Feria account and Continue integration', flush=True)
            result = subprocess.run([str(executable), str(Path(__file__).with_name('FeriaAccountSmoke.qml').resolve()),
                engine, str(urls), str(root / 'profiles')], timeout=60, env=environment)
            failures += result.returncode != 0
            urls.write_text(json.dumps([base + 'checkcookie']), encoding='utf-8')
            print(f'Testing {engine}: persistent cookies after process restart', flush=True)
            result = subprocess.run([str(executable), str(Path(__file__).with_name('BrowserSmoke.qml').resolve()),
                engine, str(urls), str(root / 'profiles')], timeout=45, env=environment)
            failures += result.returncode != 0
            urls.write_text(json.dumps([base + 'reader/chapter/1']), encoding='utf-8')
            print(f'Testing {engine}: automatic reading and resume', flush=True)
            result = subprocess.run([str(executable), str(Path(__file__).with_name('FeriaReadingSmoke.qml').resolve()),
                engine, str(urls), str(root / 'profiles')], timeout=45, env=environment)
            failures += result.returncode != 0
            other = f'http://localhost:{fixture.server_port}/fixture/'
            for qml, targets in [
                ('BrowserSmoke.qml', [base+'session-set', other+'session-set']),
                ('FeriaSignOutSmoke.qml', [f'http://127.0.0.1:{fixture.server_port}']),
                ('BrowserSmoke.qml', [base+'session-cleared'+('?qt-cleanup' if engine == 'qtwebengine' else ''), other+'session-kept'])
            ]:
                urls.write_text(json.dumps(targets), encoding='utf-8')
                print(f'Testing {engine}: sign-out {qml}', flush=True)
                result = subprocess.run([str(executable), str(Path(__file__).with_name(qml).resolve()),
                    engine, str(urls), str(root / 'profiles')], timeout=90, env=environment)
                failures += result.returncode != 0
        # The account centre clears both engines in one operation. Separate
        # single-engine tests cannot catch a broken Loader handover.
        for engine in ['webview2', 'qtwebengine']:
            urls.write_text(json.dumps([base + 'session-set', other + 'session-set']), encoding='utf-8')
            result = subprocess.run([str(executable), str(Path(__file__).with_name('BrowserSmoke.qml').resolve()),
                engine, str(urls), str(root / 'profiles')], timeout=90, env=environment)
            failures += result.returncode != 0
        urls.write_text(json.dumps([f'http://127.0.0.1:{fixture.server_port}', 'https://netflix.com',
                                   'https://www.netflix.com']), encoding='utf-8')
        print('Testing combined WebView2 and QtWebEngine sign-out, including HTTPS origin cleanup', flush=True)
        result = subprocess.run([str(executable), str(Path(__file__).with_name('FeriaSignOutSmoke.qml').resolve()),
            'both', str(urls), str(root / 'profiles')], timeout=90, env=environment)
        failures += result.returncode != 0
        for engine in ['webview2', 'qtwebengine']:
            urls.write_text(json.dumps([base + 'session-cleared' + ('?qt-cleanup' if engine == 'qtwebengine' else ''),
                                       other + 'session-kept']), encoding='utf-8')
            result = subprocess.run([str(executable), str(Path(__file__).with_name('BrowserSmoke.qml').resolve()),
                engine, str(urls), str(root / 'profiles')], timeout=90, env=environment)
            failures += result.returncode != 0
        fixture.shutdown()
        return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
