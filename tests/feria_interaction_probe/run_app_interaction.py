"""Set 3 website interaction through the full application.

The real app, headless, driven by Lanista, visits pages on a loopback server
started here. Each page asks for something a website can ask for and then
writes what it got into its own title, which is read back from the shell.

    python tests/feria_interaction_probe/run_app_interaction.py --exe native/build-msvc/colosseum.exe \
        --lanista native/build-msvc/lanista.exe --qt-bin C:/Qt/6.11.1/msvc2022_64/bin

Pass: every step passes and the app leaves by itself with exit code 0.
The sign-in page accepts only the fixture's own throwaway credentials.
"""
import argparse, base64, sys, threading, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'feria_history_probe'))
import run_app_restart as app   # shared Lanista step helpers and runner

USER, PASSWORD = 'reader', 'fixture-pass'
PAGES = {
    '/alert': "setTimeout(function(){ alert('Saved.'); document.title = 'alert:returned' }, 300)",
    '/confirm': "setTimeout(function(){ document.title = 'confirm:' + confirm('Delete this?') }, 300)",
    '/prompt': "setTimeout(function(){ document.title = 'prompt:' + prompt('Name?', 'Luffy') }, 300)",
    '/permission': "Notification.requestPermission().then(function(r){ document.title = 'asked:' + r })",
    '/remembered': "document.title = 'remembered:' + Notification.permission",
}


class Site(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def page(self, status, title, script='', headers=()):
        body = ('<!doctype html><title>%s</title><body style="background:#173044;color:white">'
                '<h1>%s</h1><script>%s</script></body>' % (title, title, script)).encode()
        self.send_response(status)
        for name, value in headers:
            self.send_header(name, value)
        self.send_header('Content-Type', 'text/html')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = self.path.split('?')[0]
        if path == '/auth':
            expected = 'Basic ' + base64.b64encode((USER + ':' + PASSWORD).encode()).decode()
            if self.headers.get('Authorization') == expected:
                self.page(200, 'auth:granted')
            else:
                self.page(401, 'auth:refused', headers=[('WWW-Authenticate', 'Basic realm="Fixture"')])
        elif path in PAGES:
            self.page(200, path[1:] + ':waiting', PAGES[path])
        else:
            self.page(200, 'fixture' + path)


def go(label, url):
    return [app.click(label + ': open the address bar', 'feriaBrowserAddressToggle'), app.pause('address bar opens', 400),
            *app.typed(label + ': type the address', 'feriaBrowserAddress', url), app.key(label + ': go', 'Return')]


def reported(label, title):
    return app.wait_for(label, 'feriaBrowserHost', 'pageTitle', title, 20000)


def asked(label):
    return app.wait_for(label, 'feriaRequestMessage', 'visible', True, 20000)


def steps(site):
    return app.boot(True) + [
        *go('alert page', site + '/alert'), asked('the alert is showing'),
        app.get('it carries the page message', 'feriaRequestMessage', 'text', '==', 'Saved.'),
        app.get('an alert has nothing to refuse', 'feriaRequestCancel', 'visible', '==', 'false'),
        app.click('OK', 'feriaRequestAccept'), reported('the page went on after the alert', 'alert:returned'),

        *go('confirm page', site + '/confirm'), asked('the confirm is showing'),
        app.get('it carries the page question', 'feriaRequestMessage', 'text', '==', 'Delete this?'),
        app.click('Cancel', 'feriaRequestCancel'), reported('the page was told no', 'confirm:false'),

        *go('prompt page', site + '/prompt'), app.wait_for('the prompt field is showing', 'feriaDialogInput', 'visible', True, 20000),
        app.get('the field starts from the page default', 'feriaDialogInput', 'text', '==', 'Luffy'),
        *app.typed('type an answer', 'feriaDialogInput', 'Zoro'),
        app.click('OK', 'feriaRequestAccept'), reported('the page got what was typed', 'prompt:Zoro'),

        *go('sign-in page', site + '/auth'), app.wait_for('the sign-in fields are showing', 'feriaSignInUser', 'visible', True, 20000),
        app.get('it names the site', 'feriaRequestMessage', 'text', '==', site + ' needs a user name and password.'),
        {'cmd': 'ui-text-input', 'label': 'type the user name', 'payload': {'target': 'feriaSignInUser', 'text': USER}},
        {'cmd': 'ui-text-input', 'label': 'type the password', 'payload': {'target': 'feriaSignInPassword', 'text': PASSWORD}},
        app.click('Sign in', 'feriaRequestAccept'), reported('the protected page opened', 'auth:granted'),

        *go('permission page', site + '/permission'), asked('the permission question is showing'),
        app.get('it names the site and the permission', 'feriaRequestMessage', 'text', '==',
                'Allow ' + site + ' to send notifications?'),
        app.click('Allow', 'feriaRequestAccept'), reported('the page was granted', 'asked:granted'),
        *go('same site again', site + '/remembered'), reported('the decision is remembered', 'remembered:granted'),
    ] + app.QUIT


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe', required=True, type=Path)
    parser.add_argument('--lanista', required=True, type=Path)
    parser.add_argument('--qt-bin', required=True, type=Path)
    parser.add_argument('--out', type=Path, default=app.ROOT / 'artifacts' / 'feria-set3-interaction')
    args = parser.parse_args()
    args.exe, args.lanista = args.exe.resolve(), args.lanista.resolve()
    args.out.mkdir(parents=True, exist_ok=True)
    server = ThreadingHTTPServer(('127.0.0.1', 0), Site)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    site = 'http://127.0.0.1:%d' % server.server_address[1]
    passed = app.run(args, 'interaction', steps(site), 'feria-set3-' + time.strftime('%Y%m%d-%H%M%S'), args.out)
    server.shutdown()
    print('INTERACTION PROOF', 'PASS' if passed else 'FAIL')
    return 0 if passed else 1


if __name__ == '__main__':
    sys.exit(main())
