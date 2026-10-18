#!/usr/bin/env python3
"""Bounded, real full-app Linux qualification. Run inside xvfb-run, never qmlscene.

Only existing Lanista read/real-window input commands are used. No models,
fixtures, provider credentials, playback, source overrides or network shims.
Exit 0 requires actual populated cards for all native datasets and clean app exit.
Screenshots remain human-review evidence; IPC duration is never load latency.
"""
import argparse
import hashlib
import json
import os
import re
import tempfile
from urllib.parse import urlsplit, urlunsplit
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import time
import uuid


def sanitize(value):
    if isinstance(value, dict):
        return {k: ('[redacted]' if re.search(r'token|password|secret|authorization|cookie|api.?key', k, re.I)
                    else sanitize(v)) for k, v in value.items()}
    if isinstance(value, list):
        return [sanitize(v) for v in value]
    if isinstance(value, str):
        def clean_url(match):
            try:
                u = urlsplit(match.group(0))
                port = u.port
            except ValueError:
                return '[redacted-url]'
            # Strip userinfo, fragments and query strings from uploaded evidence.
            host = u.hostname or ''
            if ':' in host:
                host = '[' + host + ']'
            if port:
                host += ':' + str(port)
            return urlunsplit((u.scheme, host, u.path, '', ''))
        value = re.sub(r'https?://[^\s\"<>]+', clean_url, value)
        value = re.sub(r'(?i)(bearer\s+)[A-Za-z0-9._~+/=-]+', r'\1[redacted]', value)
        value = re.sub(r'(?i)((?:token|password|secret|authorization|cookie|api[_-]?key)\s*[:=]\s*)[^\s,;]+', r'\1[redacted]', value)
        value = re.sub(r'\b(?:gh[pousr]_[A-Za-z0-9_]+|github_pat_[A-Za-z0-9_]+)\b', '[redacted]', value)
    return value


def write(path, value):
    Path(path).write_text(json.dumps(sanitize(value), indent=2) + '\n')


def sha(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def traces(path):
    result = []
    if not path.exists():
        return result
    for line in path.read_text(errors='replace').splitlines():
        marker = 'CATALOGUE_TRACE '
        if marker in line:
            try:
                result.append(json.loads(line.split(marker, 1)[1]))
            except ValueError:
                pass
    return result


def positive_area(q):
    """Intersect actual scene bounds with window and all intermediate clips."""
    if not q.get('visible') or q.get('opacity', 1) <= 0:
        return False
    r = q.get('rect', {})
    x, y = r.get('x', 0), r.get('y', 0)
    right, bottom = x + r.get('width', 0), y + r.get('height', 0)
    w = q.get('rootWindow', {})
    clips = [{'x': 0, 'y': 0, 'width': w.get('width', 0), 'height': w.get('height', 0)}]
    clips += [a['sceneRect'] for a in q.get('clipChain', [])]
    for c in clips:
        x, y = max(x, c['x']), max(y, c['y'])
        right = min(right, c['x'] + c['width'])
        bottom = min(bottom, c['y'] + c['height'])
    return right > x and bottom > y


def native_success(events, dataset, method):
    return any(e.get('component') == 'remote' and e.get('event') == 'result_delivery_end'
               and e.get('dataset') == dataset and e.get('method') == method
               and e.get('status') == 'success' for e in events)


class Driver:
    def __init__(self, output, process, pipe, profile, raw_stderr):
        self.output, self.process, self.pipe = output, process, pipe
        self.profile, self.raw_stderr = profile, raw_stderr
        self.last_click_ns = None
        self.seq, self.path = 0, None

    def call(self, cmd, payload=None, timeout=10):
        self.seq += 1
        req = {'cmd': cmd, 'seq': self.seq, 'payload': payload or {}}
        record = {'request': req, 'sentMonotonicNs': time.monotonic_ns(),
                  'sentEpochMs': time.time_ns() / 1e6}
        try:
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as conn:
                conn.settimeout(timeout)
                conn.connect(str(self.path))
                conn.sendall((json.dumps(req) + '\n').encode())
                data = bytearray()
                while b'\n' not in data:
                    chunk = conn.recv(65536)
                    if not chunk:
                        raise RuntimeError('IPC closed before complete reply')
                    data.extend(chunk)
                    if len(data) > 16 * 1024 * 1024:
                        raise RuntimeError('IPC response exceeds bounded size')
                reply = json.loads(data.split(b'\n', 1)[0])
                record['reply'] = reply
                if reply.get('type') == 'error':
                    raise RuntimeError('Lanista error: ' + json.dumps(reply))
                if reply.get('seq') != self.seq:
                    raise RuntimeError('Unexpected Lanista response sequence')
                return reply
        except Exception as error:
            record['error'] = str(error)
            raise
        finally:
            record['receivedMonotonicNs'] = time.monotonic_ns()
            record['ipcRoundTripMs'] = (record['receivedMonotonicNs'] - record['sentMonotonicNs']) / 1e6
            record['timingWarning'] = 'IPC duration is not catalogue or image load time'
            write(self.output / 'ipc' / ('%04d-%s.json' % (self.seq, cmd)), record)

    def ready(self, seconds):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                raise RuntimeError('Full app exited before IPC startup')
            candidates = [Path('/tmp') / self.pipe]
            if os.getenv('XDG_RUNTIME_DIR'):
                candidates.append(Path(os.environ['XDG_RUNTIME_DIR']) / self.pipe)
            valid = [p for p in candidates if p.is_socket() and p.stat().st_uid == os.getuid()]
            if len(valid) == 1:
                self.path = valid[0]
                state = self.call('get-state')
                if self.call('qml-get', {'object': 'bootSplash', 'props': ['visible']})['props'].get('visible') is False:
                    return state
            time.sleep(0.5)
        raise TimeoutError('Startup/boot deadline, not catalogue deadline, exceeded')

    def click(self, name):
        # Fresh handles avoid hidden retained headers with duplicate objectNames.
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            snap = self.call('ui-snapshot')
            found = []
            for item in snap.get('elements', []):
                if item.get('objectName') == name and item.get('enabled'):
                    if positive_area(self.call('ui-query', {'object': item['handle']})):
                        found.append(item)
            if len(found) == 1:
                self.last_click_ns = time.monotonic_ns()
                return self.call('ui-click', {'target': found[0]['handle']})
            if len(found) > 1:
                raise RuntimeError('Ambiguous visible control: ' + name)
            time.sleep(0.25)
        raise RuntimeError('Visible control deadline exceeded: ' + name)

    def grab(self, name, object_name):
        result = self.call('qml-get', {'object': object_name, 'props': ['visible'],
                                      'grab': {'target': 'window'}})
        path = Path(result['grabPath']).resolve()
        if not path.is_relative_to(self.profile) or not path.is_file():
            raise RuntimeError('Screenshot outside owned profile or missing')
        shutil.copyfile(path, self.output / (name + '.png'))

    def cards(self, prefix):
        snap = self.call('ui-snapshot')
        found = []
        for item in snap.get('elements', []):
            if not item.get('objectName', '').startswith(prefix):
                continue
            handle = item['handle']
            geometry = self.call('ui-query', {'object': handle})
            if not positive_area(geometry):
                continue
            props = self.call('qml-get', {'object': handle, 'props': ['capText', 'skeleton', 'visible']})['props']
            if props.get('capText') and props.get('skeleton') is False:
                found.append({'objectName': item['objectName'], 'properties': props, 'geometry': geometry})
            if len(found) >= 8:
                break
        return found

    def journey(self, name, dataset, method, object_name, expected, prefix, seconds):
        started = time.monotonic_ns()
        deadline = time.monotonic() + seconds
        props = {}
        visible_cards = []
        while time.monotonic() < deadline:
            props = self.call('qml-get', {'object': object_name, 'props': [
                'currentType', 'automationItemCount', 'loading', 'pageFailed',
                'pageKey', 'liveShelfCount', 'errorText', 'active']})['props']
            if any(props.get(k) != v for k, v in expected.items()):
                time.sleep(0.5)
                continue
            if not props.get('loading'):
                visible_cards = self.cards(prefix)
                if visible_cards or props.get('pageFailed') or props.get('errorText'):
                    break
            time.sleep(0.5)
        observed = time.monotonic_ns()
        self.grab(name, object_name)
        events = traces(self.raw_stderr)
        success = (bool(visible_cards) and (props.get('automationItemCount') or props.get('liveShelfCount') or 0) > 0
                   and not props.get('loading')
                   and not props.get('pageFailed') and not props.get('errorText')
                   and all(props.get(k) == v for k, v in expected.items())
                   and native_success(events, dataset, method))
        result = {'name': name, 'dataset': dataset, 'method': method,
                  'properties': props, 'visibleCards': visible_cards, 'passed': success,
                  'navigationClickSentMonotonicNs': self.last_click_ns,
                  'firstPopulatedCardObservedSinceClickWithinMs': (observed - self.last_click_ns) / 1e6 if visible_cards else None,
                  'observationStartedMonotonicNs': started, 'observedMonotonicNs': observed,
                  'firstPopulatedCardObservedWithinMs': (observed - started) / 1e6 if visible_cards else None,
                  'timingWarning': 'Polling upper bound since observation began, not exact paint or request latency. Native fetch may begin at startup.',
                  'screenshot': name + '.png'}
        write(self.output / (name + '.json'), result)
        return result


def preflight(source, binary):
    if not os.getenv('DISPLAY') or os.getenv('QT_QPA_PLATFORM', 'xcb') != 'xcb':
        raise RuntimeError('Actual X11 DISPLAY/xcb required; use xvfb-run')
    # Do not start/adopt an unrelated stream service. No source changes needed.
    if os.getenv('COLOSSEUM_STREAM_SERVER'):
        raise RuntimeError('Unexpected stream-server override')
    for p in [binary.parent / 'stream_server', binary.parent.parent / 'stream_server', Path('/usr/share/stremio-service')]:
        if (p / 'server.js').exists():
            raise RuntimeError('Stream runtime present; this catalogue-only job must not bundle it')
    with socket.socket() as s:
        s.settimeout(0.3)
        if s.connect_ex(('127.0.0.1', 11470)) == 0:
            raise RuntimeError('Existing stream service listener; refusing unrelated engine adoption')
    for root in [source / 'data', binary.parent / '../../data']:
        for name in ['comics_catalog.db', 'mal_catalog.db', 'imdb_catalog.db', 'tankoban_catalog.db']:
            if (root / name).exists():
                raise RuntimeError('Runtime catalogue DB fallback exists: ' + str(root / name))
    for key in ['COLOSSEUM_TEST_CATALOGUE_ORIGIN', 'COLOSSEUM_TEST_CATALOGUE_OFFLINE',
                'COLOSSEUM_QML_PATH', 'COLOSSEUM_LANISTA_WRITE', 'COLOSSEUM_CATALOG_SELFTEST']:
        if os.getenv(key):
            raise RuntimeError('Unexpected test/write override: ' + key)


def self_test():
    q = {'visible': True, 'opacity': 1, 'rect': {'x': 2, 'y': 2, 'width': 5, 'height': 5},
         'rootWindow': {'width': 20, 'height': 20}, 'clipChain': []}
    assert positive_area(q)
    q['clipChain'] = [{'sceneRect': {'x': 10, 'y': 10, 'width': 5, 'height': 5}}]
    assert not positive_area(q)
    e = {'component': 'remote', 'event': 'result_delivery_end', 'dataset': 'imdb', 'method': 'landing', 'status': 'failure'}
    assert not native_success([e], 'imdb', 'landing')
    e['status'] = 'success'
    assert native_success([e], 'imdb', 'landing')
    e['event'] = 'result_ready'
    assert not native_success([e], 'imdb', 'landing')
    assert sanitize({'token': 'abc'}) == {'token': '[redacted]'}
    assert sanitize('https://u:p@example.com/a?q=private#f') == 'https://example.com/a'
    assert sanitize('Bearer abc123') == 'Bearer [redacted]'
    print('8 pure qualification helper checks passed')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary', type=Path)
    p.add_argument('--lanista', type=Path, help='Record companion identity; IPC uses its published JSON protocol')
    p.add_argument('--source', type=Path, default=Path.cwd())
    p.add_argument('--output', type=Path)
    p.add_argument('--startup-timeout', type=int, default=90)
    p.add_argument('--catalogue-timeout', type=int, default=60)
    p.add_argument('--poster-seconds', type=int, default=20)
    p.add_argument('--self-test', action='store_true')
    a = p.parse_args()
    if a.self_test:
        self_test()
        return 0
    if not a.binary or not a.lanista or not a.output:
        p.error('--binary, --lanista and --output required')
    if not (1 <= a.startup_timeout <= 120 and 1 <= a.catalogue_timeout <= 90 and 0 <= a.poster_seconds <= 30):
        p.error('Timeout exceeds bounded qualification budget')
    source, binary, output = a.source.resolve(), a.binary.resolve(), a.output.resolve()
    if output.exists():
        raise RuntimeError('Output must be absent for an honestly empty isolated profile')
    preflight(source, binary)
    manifest = binary.parent / 'qml-build.manifest'
    if not manifest.is_file():
        raise RuntimeError('Production QML manifest required')
    (output / 'ipc').mkdir(parents=True)
    private = Path(tempfile.mkdtemp(prefix='b1-native-private-', dir=output.parent.parent))
    profile = private / 'profile'
    for d in ['data', 'cache', 'config']:
        (profile / d).mkdir(parents=True)
    tag = 'b1-ci-' + uuid.uuid4().hex[:12]
    env = os.environ.copy()
    # CI credentials are not app inputs and must not leak into this process.
    for key in list(env):
        if any(part in key.upper() for part in ('TOKEN', 'PASSWORD', 'SECRET', 'API_KEY')):
            del env[key]
    env.update(XDG_DATA_HOME=str(profile / 'data'), XDG_CACHE_HOME=str(profile / 'cache'),
               XDG_CONFIG_HOME=str(profile / 'config'), QT_QPA_PLATFORM='xcb',
               COLOSSEUM_APPDATA_TAG=tag, COLOSSEUM_LANISTA_PIPE=tag,
               COLOSSEUM_LANISTA_DRIVE='1', COLOSSEUM_AUTOMATION_FOCUS_SEAMS='0',
               COLOSSEUM_CATALOGUE_TRACE='1', COLOSSEUM_POSTER_TIMING='1')
    # Keep normal network policy and any platform proxy env unchanged. No proxy bootstrap.
    cache = binary.parent / 'CMakeCache.txt'
    configured_endpoint = None
    if cache.exists():
        for line in cache.read_text().splitlines():
            if line.startswith('COLOSSEUM_CONTENT_API_URL:STRING='):
                configured_endpoint = line.split('=', 1)[1]
    receipt = {'configuredContentEndpoint': configured_endpoint,
               'binarySha256': sha(binary), 'lanistaSha256': sha(a.lanista),
               'qmlManifestSha256': sha(manifest),
               'sourceCommit': subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip(),
               'sourceStatus': subprocess.check_output(['git', '-C', str(source), 'status', '--porcelain'], text=True),
               'driverSha256': sha(__file__), 'profileInitiallyEmpty': True,
               'rendering': 'full native app under X11; CI supplies Xvfb, local validation supplies owned desktop',
               'proxyVariableNamesPresent': [k for k in env if k.lower() in ('http_proxy', 'https_proxy', 'all_proxy', 'no_proxy')],
               'systemProxyOptIn': env.get('COLOSSEUM_USE_SYSTEM_PROXY', ''),
               'coldDefinition': 'Empty local profile only; origin/CDN and OS cache state uncontrolled',
               'launchMonotonicNs': time.monotonic_ns(), 'launchEpochMs': time.time_ns() / 1e6}
    write(output / 'launch.json', receipt)
    results, error, driver, process = [], None, None, None
    raw_stdout, raw_stderr = private / 'stdout.log', private / 'stderr.log'
    stdout, stderr = open(raw_stdout, 'w'), open(raw_stderr, 'w')
    try:
        process = subprocess.Popen([str(binary)], cwd=source, env=env, stdout=stdout, stderr=stderr)
        receipt['pid'] = process.pid
        write(output / 'launch.json', receipt)
        driver = Driver(output, process, tag, profile, raw_stderr)
        state = driver.ready(a.startup_timeout)
        write(output / 'initial-state.json', state)
        if not Path(state.get('appDataRoot', '')).is_relative_to(profile / 'data'):
            raise RuntimeError('Actual appDataRoot is not isolated profile')
        driver.click('modePill_Tankoban')
        driver.click('tankobanDiscoverType_comics')
        results.append(driver.journey('comics', 'comics', 'discoverPageFiltered', 'tankobanDiscoverBrowser',
                                      {'currentType': 'comics'}, 'discoverCard_', a.catalogue_timeout))
        driver.click('tankobanDiscoverType_manga')
        results.append(driver.journey('manga', 'mal', 'discoverPageFiltered', 'tankobanDiscoverBrowser',
                                      {'currentType': 'manga'}, 'mangaDiscoverCard_', a.catalogue_timeout))
        driver.click('modePill_Theatre')
        driver.click('theatreTab_movies')
        results.append(driver.journey('imdb', 'imdb', 'landing', 'theatreCatalogPage',
                                      {'pageKey': 'movies', 'active': True}, 'theatreCatalogCard_', a.catalogue_timeout))
        # Automatic poster requests are allowed throughout; no separate API fetches.
        time.sleep(a.poster_seconds)
        driver.grab('imdb-posters', 'theatreCatalogPage')
    except Exception as exc:
        error = str(exc)
    finally:
        forced = False
        if process and process.poll() is None:
            try:
                if driver and driver.path:
                    try:
                        driver.call('ui-keypress', {'key': 'Ctrl+Q'}, timeout=3)
                    except (OSError, RuntimeError):
                        pass
                process.wait(timeout=15)
            except Exception:
                forced = True
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
        stdout.close()
        stderr.close()
        exit_code = process.returncode if process else None
        write(output / 'exit.json', {'numericExitStatus': exit_code, 'forcedTermination': forced})
        write(output / 'profile-databases-after.json', [str(x.relative_to(profile)) for x in profile.rglob('*.db')])
    events = traces(raw_stderr)
    # Raw logs remain in private sibling storage, never in artifact output.
    diagnostics = ['Proxy connection refused', 'Network unreachable', 'Socket operation timed out', 'STATIC_HTTP_0']
    raw_text = raw_stderr.read_text(errors='replace')
    (output / 'stderr.sanitized.log').write_text('\n'.join(f'{term}: {raw_text.count(term)} occurrences' for term in diagnostics) + '\n')
    (output / 'stdout.sanitized.log').write_text('Raw stdout retained locally outside artifact output; bytes=' + str(raw_stdout.stat().st_size) + '\n')
    write(output / 'catalogue-trace.json', events)
    starts, deliveries = {}, []
    for event in events:
        if event.get('component') != 'remote':
            continue
        key = (event.get('remoteId'), event.get('dataset'), event.get('queryKey'), event.get('sourceGeneration'))
        if event.get('event') == 'enqueue':
            starts[key] = event
        elif event.get('event') == 'result_delivery_end' and key in starts:
            begin = starts[key]
            duration = event.get('monotonicMs', 0) - begin.get('monotonicMs', 0)
            deliveries.append({'dataset': event.get('dataset'), 'method': event.get('method'),
                               'queryKey': event.get('queryKey'), 'status': event.get('status'),
                               'reason': event.get('reason'), 'requestToDeliveryMs': duration,
                               'classification': 'native-data-delivered' if event.get('status') == 'success' else 'failure-latency',
                               'timingBasis': 'Same-process native monotonic enqueue to result_delivery_end; not server latency or visual paint'})
    write(output / 'native-delivery-timings.json', deliveries)
    probes = list(profile.rglob('poster-timing.json'))
    poster = {'probeAvailable': bool(probes), 'warning': 'Image decode/paint requires visible-card correlation; local SVG/resources excluded.'}
    if probes:
        probe_data = json.loads(probes[0].read_text())
        write(output / 'poster-timing.json', probe_data)
        rows = probe_data.get('rows', [])
        remote = [r for r in rows if r.get('url', '').startswith(('http://', 'https://'))]
        visible_names = {c['objectName'] for r in results for c in r['visibleCards']}
        correlated = [r for r in remote if r.get('objectName', '').removesuffix('_art_img') in visible_names]
        poster['visibleCardImageRows'] = correlated
        poster['visibleCardDecodedRows'] = sum(r.get('decodedMs', -1) >= 0 for r in correlated)
        poster['visibleCardPaintRows'] = sum(r.get('paintMs', -1) >= 0 for r in correlated)
        poster.update(httpRows=len(remote), positiveByteRows=sum(r.get('bytes', 0) > 0 for r in remote),
                      decodedRows=sum(r.get('decodedMs', -1) >= 0 for r in remote),
                      paintRows=sum(r.get('paintMs', -1) >= 0 for r in remote))
    passed = not error and len(results) == 3 and all(r['passed'] for r in results) and exit_code == 0 and not forced
    write(output / 'summary.json', {'cataloguesPassed': passed, 'journeys': results, 'error': error,
                                   'posters': poster, 'numericExitStatus': exit_code,
                                   'warmRunPerformed': False,
                                   'timingWarning': 'Native trace success must be checked; failed/empty transfers and IPC timings are not loads. No server latency or exact visual paint duration is inferred.'})
    print(json.dumps(sanitize({'cataloguesPassed': passed, 'error': error, 'numericExitStatus': exit_code})))
    return 0 if passed else 1


if __name__ == '__main__':
    sys.exit(main())
