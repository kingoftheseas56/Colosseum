#!/usr/bin/env python3
"""Isolated Linux and Apple Silicon updater rehearsal. Never used for release builds."""
import argparse
from contextlib import contextmanager
import hashlib
import http.server
import json
import os
from pathlib import Path
import re
import shutil
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
import time


ORIGIN = 'https://localhost:18443'
DOWNLOAD_ROOT = '/updates/downloads/v1.5.1/'
KINDS = {
    'appimage': ('linux-x64-appimage', 'Colosseum-{}-x86_64.AppImage', 'Download AppImage'),
    'deb': ('linux-x64-deb', 'Colosseum-{}-linux-amd64.deb', 'Download .deb'),
    'dmg': ('macos-arm64-dmg', 'Colosseum-{}-macOS-arm64.dmg', 'Download DMG'),
}
MANIFEST_NAME = 'colosseum-update-v2.json'
OPENSSL = os.environ.get('STAGING_OPENSSL', 'openssl')


def run(*args, **kwargs):
    return subprocess.run([str(item) for item in args], check=True, **kwargs)


def change(path, before, after):
    code = path.read_text()
    if code.count(before) != 1:
        raise RuntimeError(f'Expected one source anchor in {path}: {before}')
    path.write_text(code.replace(before, after, 1))


def prepare(source, private):
    private.mkdir(mode=0o700)
    update_key = private / 'update.pem'
    run(OPENSSL, 'genpkey', '-algorithm', 'ED25519', '-out', update_key)
    der = run(OPENSSL, 'pkey', '-in', update_key, '-pubout', '-outform', 'DER',
              capture_output=True).stdout
    if len(der) != 44 or der[:12].hex() != '302a300506032b6570032100':
        raise RuntimeError('Unexpected temporary Ed25519 public key format')
    source_header = source / 'native/update/UpdateTestPublicKey.h'
    key_rows = [', '.join(f'0x{value:02x}' for value in der[start:start + 8])
                for start in range(12, 44, 8)]
    code, replacements = re.subn(r'(?<=kUpdateTestPublicKey = \{\n)[\s\S]*?(?=\n\};)',
                                 '    ' + ',\n    '.join(key_rows) + ',', source_header.read_text())
    if replacements != 1:
        raise RuntimeError('Temporary updater trust header did not match')
    source_header.write_text(code)
    run(OPENSSL, 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1',
        '-subj', '/CN=localhost', '-addext', 'subjectAltName=DNS:localhost',
        '-keyout', private / 'https.pem', '-out', private / 'https.crt',
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    client = source / 'native/update/UpdateReleaseClient.cpp'
    change(client, '#include <QTimer>', '#include <QTimer>\n#include <QSslCertificate>\n#include <QSslConfiguration>')
    change(client, '    QNetworkRequest request(url);', '''    QNetworkRequest request(url);
    // Trust only the disposable localhost certificate for rehearsal requests.
    if (url.host() == QLatin1String("localhost") && url.port() == 18443) {
        auto ssl = request.sslConfiguration();
        ssl.setCaCertificates(QSslCertificate::fromPath(qEnvironmentVariable("COLOSSEUM_STAGING_CA")));
        request.setSslConfiguration(ssl);
    }''')
    change(source / 'native/main.cpp',
           '    updateHooks.openUrl = [](const QUrl& url) { return QDesktopServices::openUrl(url); };',
           '''    updateHooks.openUrl = [](const QUrl& url) {
        const bool opened = QDesktopServices::openUrl(url);
        qInfo().noquote() << "STAGING_UPDATE_OPEN:" << url.toString() << opened;
        return opened;
    };''')


def digest(path):
    with path.open('rb') as block:
        return hashlib.file_digest(block, 'sha256').hexdigest()


def package(path):
    return dict(asset=path.name, size=path.stat().st_size, sha256=digest(path))


@contextmanager
def feed(private, latest, receipt):
    downloads = {KINDS[kind][0]: package(path) for kind, path in latest.items()}
    manifest = dict(schemaVersion=2, version='1.5.1', tag='v1.5.1',
                    eyebrow='Colosseum staging', title='Colosseum 1.5.1 rehearsal',
                    summary='Disposable upgrade fixture', installer=next(iter(downloads.values())),
                    minimumUpdaterVersion='1.0.0', highlights=[], artwork=[], downloads=downloads,
                    notesUrl='https://getcolosseum.world/releases/v1.5.1')
    manifest_path = private / MANIFEST_NAME
    manifest_path.write_text(json.dumps(manifest, separators=(',', ':')))
    signature_path = private / (MANIFEST_NAME + '.sig')
    run(OPENSSL, 'pkeyutl', '-sign', '-rawin', '-inkey', private / 'update.pem',
        '-in', manifest_path, '-out', signature_path)
    signature = signature_path.read_bytes()
    if len(signature) != 64:
        raise RuntimeError('Expected a raw 64-byte Ed25519 signature')
    bad_signature = bytes([signature[0] ^ 1]) + signature[1:]
    package_paths = {DOWNLOAD_ROOT + path.name: path for path in latest.values()}
    receipt['signing'] = 'ephemeral Ed25519 fixture and scoped ephemeral TLS certificate'
    receipt['manifest'] = manifest
    receipt['requests'] = []
    state = {'bad_signature': False}

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            receipt['requests'].append(self.path)
            sig = bad_signature if state['bad_signature'] else signature
            files = {'/updates/' + MANIFEST_NAME: manifest_path.read_bytes(),
                     '/updates/' + MANIFEST_NAME + '.sig': sig}
            if self.path == '/updates/stable.json':
                assets = [dict(name=Path(name).name, size=len(content),
                               digest='sha256:' + hashlib.sha256(content).hexdigest(),
                               browser_download_url=ORIGIN + name) for name, content in files.items()]
                assets.extend(dict(name=entry['asset'], size=entry['size'],
                                   digest='sha256:' + entry['sha256'],
                                   browser_download_url=ORIGIN + DOWNLOAD_ROOT + entry['asset'])
                              for entry in downloads.values())
                body = json.dumps(dict(tag_name='v1.5.1', draft=False,
                                       prerelease=False, assets=assets)).encode()
                self.send_response(200)
                self.send_header('Content-Length', str(len(body)))
                self.send_header('Content-Type', 'application/json')
                self.end_headers()
                self.wfile.write(body)
            elif self.path in files:
                body = files[self.path]
                self.send_response(200)
                self.send_header('Content-Length', str(len(body)))
                self.end_headers()
                self.wfile.write(body)
            elif self.path in package_paths:
                path = package_paths[self.path]
                self.send_response(200)
                self.send_header('Content-Length', str(path.stat().st_size))
                self.send_header('Content-Type', 'application/octet-stream')
                self.send_header('Content-Disposition', f'attachment; filename="{path.name}"')
                self.end_headers()
                with path.open('rb') as stream:
                    shutil.copyfileobj(stream, self.wfile)
            else:
                self.send_error(404)

        def log_message(self, *_):
            pass

    with http.server.ThreadingHTTPServer(('localhost', 18443), Handler) as server:
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(private / 'https.crt', private / 'https.pem')
        server.socket = context.wrap_socket(server.socket, server_side=True)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            yield state
        finally:
            server.shutdown()
            thread.join(timeout=5)


def ipc(address, command, payload=None):
    with socket.socket(socket.AF_UNIX) as connection:
        connection.settimeout(8)
        connection.connect(str(address))
        connection.sendall(json.dumps(dict(seq=1, cmd=command, payload=payload or {})).encode() + b'\n')
        response = connection.makefile('rb').readline()
    result = json.loads(response)
    if result.get('type') != 'reply' or result.get('ok') is False:
        raise RuntimeError(f'Lanista {command} failed: {result}')
    return result


class Application:
    def __init__(self, command, profile, private, work, output, label):
        self.command, self.profile, self.work, self.output = command, profile, work, output
        self.address = work / 'lanista.sock'
        self.log = output / f'{label}.log'
        self.trace = output / f'{label}.jsonl'
        self.env = dict(os.environ, COLOSSEUM_LANISTA_PIPE=str(self.address),
                        COLOSSEUM_APPDATA_TAG=profile, COLOSSEUM_LANISTA_DRIVE='1',
                        COLOSSEUM_STAGING_CA=str(private / 'https.crt'),
                        QT_FORCE_STDERR_LOGGING='1')

    def call(self, command, payload=None):
        result = ipc(self.address, command, payload)
        with self.trace.open('a') as log:
            log.write(json.dumps(dict(command=command, payload=payload, result=result)) + '\n')
        return result

    def property(self, target, name):
        try:
            return ipc(self.address, 'qml-get', dict(object=target, props=[name])).get('props', {}).get(name)
        except (OSError, RuntimeError, ValueError):
            return None

    def await_value(self, target, name, value, timeout=45):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline and self.process.poll() is None:
            if self.property(target, name) == value:
                self.call('qml-get', dict(object=target, props=[name]))
                return
            time.sleep(0.4)
        raise RuntimeError(f'Expected {target}.{name}={value!r}, got {self.property(target, name)!r}')

    def click(self, name):
        self.call('ui-click', dict(target=name))

    @contextmanager
    def launch(self, version, *, onboard):
        self.address.unlink(missing_ok=True)
        printed_version = run(self.command, '--version', env=self.env, capture_output=True, text=True,
                              timeout=45).stdout.strip()
        if printed_version != f'Colosseum {version}':
            raise RuntimeError(f'Wrong executable identity from {self.command}: {printed_version}')
        with self.log.open('w') as log:
            self.process = subprocess.Popen([str(self.command)], cwd=self.work, env=self.env,
                                            stdout=log, stderr=subprocess.STDOUT)
            try:
                self.await_value('bootSplash', 'visible', False, timeout=130)
                welcome = 'accountWelcomeContinueLocal'
                if onboard:
                    self.await_value(welcome, 'visible', True, timeout=30)
                    self.click(welcome)
                self.await_value('colosseumTopbarUpdateButton', 'visible', True)
                state = self.call('get-state')
                if self.profile not in state.get('appDataRoot', '') or self.profile not in state.get('cacheRoot', ''):
                    raise RuntimeError('The package escaped its isolated profile')
                if self.property(welcome, 'visible') is True:
                    raise RuntimeError('Local-only onboarding was not retained')
                yield state
                self.call('ui-keypress', {'key': 'Ctrl+Q'})
                if self.process.wait(timeout=45) != 0:
                    raise RuntimeError('Package did not quit normally')
            finally:
                if self.process.poll() is None:
                    self.process.terminate()
                    try:
                        self.process.wait(timeout=12)
                    except subprocess.TimeoutExpired:
                        self.process.kill()
                        self.process.wait(timeout=5)

    def open_updates(self):
        self.click('colosseumTopbarUpdateButton')
        self.await_value('colosseumUpdatePage', 'visible', True)

    def return_to_home(self):
        self.click('colosseumUpdateBackAction')
        self.await_value('colosseumTopbarUpdateButton', 'visible', True)

    def package_url(self, filename):
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            opens = re.findall(r'STAGING_UPDATE_OPEN: (\S+) (true|false)', self.log.read_text(errors='replace'))
            if opens:
                expected = (ORIGIN + DOWNLOAD_ROOT + filename, 'true')
                if opens != [expected]:
                    raise RuntimeError(f'Unexpected package URL or browser handoff: {opens}')
                return expected[0]
            time.sleep(0.4)
        raise RuntimeError('No OS openUrl result observed')


def install(kind, payload, install_root, work):
    if kind == 'deb':
        run('sudo', '-n', 'apt-get', '-y', '--no-install-recommends', 'install', payload)
        payload_version = run('dpkg-deb', '--field', payload, 'Version', capture_output=True, text=True).stdout.strip()
        installed_version = run('dpkg-query', '-W', '-f=${Version}', 'colosseum', capture_output=True, text=True).stdout.strip()
        if not payload_version or installed_version != payload_version:
            raise RuntimeError('Installed DEB identity does not match the payload')
        return Path('/usr/bin/colosseum')
    if kind == 'appimage':
        target = install_root / 'Colosseum.AppImage'
        replacement = install_root / 'Colosseum.AppImage.new'
        shutil.copyfile(payload, replacement)
        replacement.chmod(0o755)
        replacement.replace(target)
        return target
    mount = work / 'mounted-dmg'
    mount.mkdir(exist_ok=True)
    run('hdiutil', 'attach', '-readonly', '-nobrowse', '-mountpoint', mount, payload)
    try:
        replacement = install_root / 'Colosseum.next.app'
        installed = install_root / 'Colosseum.app'
        run('ditto', mount / 'Colosseum.app', replacement)
        run('codesign', '--verify', '--deep', '--strict', replacement)
        if installed.exists():
            installed.rename(work / 'previous.app')
        replacement.rename(installed)
    finally:
        run('hdiutil', 'detach', mount)
    info = run('/usr/libexec/PlistBuddy', '-c', 'Print :CFBundleIdentifier',
               installed / 'Contents/Info.plist', capture_output=True, text=True).stdout.strip()
    if info != 'world.getcolosseum.Colosseum':
        raise RuntimeError(f'Unexpected bundle identifier: {info}')
    return installed / 'Contents/MacOS/colosseum'


def qualify(args):
    args.output.mkdir(parents=True, exist_ok=True)
    receipt = {'status': 'FAIL', 'platform': sys.platform, 'source': args.source, 'formats': {}}
    versions = {version: {kind: args.packages / version / KINDS[kind][1].format(version)
                          for kind in args.kinds} for version in ('1.5.0', '1.5.1')}
    signature_request = '/updates/' + MANIFEST_NAME + '.sig'
    try:
        with tempfile.TemporaryDirectory(prefix='col-staging-', dir='/tmp') as directory, \
                feed(args.private, versions['1.5.1'], receipt) as server:
            work = Path(directory)
            for kind in args.kinds:
                record = receipt['formats'][kind] = {'status': 'FAIL'}
                old, new = versions['1.5.0'][kind], versions['1.5.1'][kind]
                record['bytes'] = [package(old), package(new)]
                install_root = work / kind
                install_root.mkdir()
                executable = install(kind, old, install_root, work)
                profile = f'staging-{kind}-{os.getpid()}'
                pre = Application(executable, profile, args.private, work, args.output, f'{kind}-before')
                with pre.launch('1.5.0', onboard=True) as before:
                    pre.await_value('colosseumTopbarUpdateBadge', 'visible', True, timeout=90)
                    pre.open_updates()
                    pre.await_value('colosseumUpdatePage', 'automationState', 'ManualUpdateRequired')
                    pre.await_value('colosseumUpdatePage', 'automationVersion', '1.5.1')
                    primary = 'dmg' if kind == 'dmg' else 'appimage'
                    pre.await_value('colosseumUpdatePage', 'primaryLabel', KINDS[primary][2])
                    pre.await_value('colosseumUpdateAlternateDownload', 'visible', kind != 'dmg')
                    if kind != 'dmg':
                        pre.await_value('colosseumUpdateAlternateDownload', 'text', KINDS['deb'][2])
                    target = ('colosseumUpdateAlternateDownload' if kind == 'deb'
                              else 'colosseumUpdatePrimaryAction')
                    pre.click(target)
                    url = pre.package_url(new.name)
                downloaded = install_root / new.name
                run('curl', '--fail', '--silent', '--show-error', '--cacert', args.private / 'https.crt',
                    '--noproxy', 'localhost', '--output', downloaded, url, timeout=150)
                if package(downloaded) != package(new):
                    raise RuntimeError('Downloaded bytes do not match the signed package metadata')
                executable = install(kind, downloaded, install_root, work)
                post = Application(executable, profile, args.private, work, args.output, f'{kind}-after')
                with post.launch('1.5.1', onboard=False) as after:
                    post.open_updates()
                    post.await_value('colosseumUpdatePage', 'automationState', 'UpToDate')
                    post.await_value('colosseumUpdatePage', 'automationVersion', '1.5.1')
                    post.await_value('colosseumUpdateStatusText', 'text', 'Everything is up to date')
                    post.return_to_home()
                    post.await_value('colosseumTopbarUpdateBadge', 'visible', False)
                    post.open_updates()
                    post.await_value('colosseumUpdatePage', 'primaryLabel', 'Check again')
                    requests_before_manual_check = len(receipt['requests'])
                    # A recent authenticated result is intentionally reused on relaunch.
                    # Test a fresh network check through the real on-page action.
                    post.click('colosseumUpdatePrimaryAction')
                    deadline = time.monotonic() + 30
                    while signature_request not in receipt['requests'][requests_before_manual_check:]:
                        if time.monotonic() >= deadline:
                            raise RuntimeError('Check again did not fetch the signed feed after upgrade')
                        time.sleep(0.3)
                    post.await_value('colosseumUpdatePage', 'automationState', 'UpToDate')
                    post.await_value('colosseumUpdateStatusText', 'text', 'Everything is up to date')
                    post.await_value('colosseumUpdatePage', 'primaryLabel', 'Check again')
                    post.return_to_home()
                    post.await_value('colosseumTopbarUpdateBadge', 'visible', False)
                if before['appDataRoot'] != after['appDataRoot'] or before['cacheRoot'] != after['cacheRoot']:
                    raise RuntimeError('Upgrade changed the application profile')
                record.update(status='PASS', launched=['1.5.0', '1.5.1'], download_url=url,
                              profile_retained=True, badge_cleared=True,
                              cached_release_up_to_date=True, manual_verified_feed_recheck=True,
                              download_scope='QDesktopServices result observed. Harness fetched that HTTPS URL and installed the verified bytes.')
            server['bad_signature'] = True
            invalid = Application(executable, f'staging-invalid-{os.getpid()}', args.private,
                                  work, args.output, 'invalid-signature')
            with invalid.launch('1.5.1', onboard=True):
                invalid.open_updates()
                invalid.await_value('colosseumUpdateStatusText', 'text', 'Could not verify the release')
                invalid.return_to_home()
                invalid.await_value('colosseumTopbarUpdateBadge', 'visible', False)
            receipt.update(status='PASS', invalid_signature='rejected with envelope digest matching the tampered signature')
    finally:
        (args.output / 'rehearsal.json').write_text(json.dumps(receipt, indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    setup = commands.add_parser('prepare')
    setup.add_argument('source', type=Path)
    setup.add_argument('private', type=Path)
    exercise = commands.add_parser('exercise')
    exercise.add_argument('--output', type=Path, required=True)
    exercise.add_argument('--private', type=Path, required=True)
    exercise.add_argument('--packages', type=Path, required=True)
    exercise.add_argument('--source', required=True)
    exercise.add_argument('kinds', nargs='+', choices=tuple(KINDS))
    args = parser.parse_args()
    if args.command == 'prepare':
        prepare(args.source, args.private)
    else:
        qualify(args)


if __name__ == '__main__':
    main()
