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


def positive_integer(value):
    try:
        number = int(str(value or '0'))
        return number if number > 0 else 0
    except (TypeError, ValueError):
        return 0


def query_key(method, args):
    return hashlib.sha256(json.dumps({'args': args, 'method': method}, ensure_ascii=False,
                                     separators=(',', ':'), sort_keys=True).encode()).hexdigest()


def deep_cover_assessment(cases, probe_rows):
    decoded = {r.get('url') for r in probe_rows if r.get('decodedMs', -1) >= 0}
    checks, missing = [], []
    for case in cases:
        if '-warm-' in case.get('case', ''):
            continue
        if 'nativeVolumes' in case:
            for row in case['nativeVolumes']:
                number = str(row.get('number', ''))
                images = case.get('observedVolumeCards', {}).get(number, {}).get('imageStates', [])
                checks.append((case['case'], number, row.get('cover') or '', images))
        if 'nativeEditions' in case:
            observed = {str(r.get('chId', '')): r for r in case.get('observedEditionRows', [])}
            for row in case['nativeEditions']:
                identity = str(row.get('chid') or '')
                checks.append((case['case'], identity, row.get('cover') or '', observed.get(identity, {}).get('images', [])))
        if 'displayedReleaseRows' in case:
            for row in case['displayedReleaseRows']:
                checks.append((case['case'], str(row.get('relId', '')), '', row.get('images', [])))
    results = []
    for case, identity, expected_url, images in checks:
        images = [i for i in images if str(i.get('source', '')).startswith(('https://', 'http://'))]
        sources = [str(i.get('source', '')) for i in images if i.get('source')]
        if not expected_url and not sources:
            missing.append({'case': case, 'id': identity, 'field': 'optional cover URL absent'})
            continue
        ready = any(i.get('status') == 1 for i in images) or bool(({expected_url} | set(sources)) & decoded)
        failed = not ready and any(i.get('status') == 3 for i in images)
        results.append({'case': case, 'id': identity, 'expectedCoverUrl': expected_url, 'observedImages': images,
                        'status': 'PASS' if ready else 'FAIL' if failed else 'INCOMPLETE'})
    status = 'FAIL' if any(r['status'] == 'FAIL' for r in results) else 'INCOMPLETE' if not results or any(r['status'] == 'INCOMPLETE' for r in results) else 'PASS'
    return {'case': 'deep-cover-rendering', 'status': status, 'providedCoverChecks': results,
            'missingOptionalFields': missing, 'note': 'Missing upstream optional URL is not an app defect. Ready/decoded proves decoding, not an exact pixel-paint timestamp.'}


def assess_deep(cases):
    required = {f'{kind}-series-{n}' for kind in ('manga', 'comics') for n in (1, 2, 3)}
    required |= {f'{kind}-{operation}' for kind in ('manga', 'comics') for operation in ('pagination', 'warm-reopen', 'representative-classes')}
    required |= {'tankoban-empty-search', 'theatre-empty-search', 'theatre-movie', 'theatre-show', 'comics-gcd-search-releases', 'deep-cover-rendering'}
    optional = {'forced-error-recovery', 'forced-reload', 'all-catalogue-records'}
    indexed = {c['case']: c for c in cases}
    missing = sorted(required - set(indexed))
    incomplete = missing + [c['case'] for c in cases if c.get('status') in ('INCOMPLETE', 'BLOCKED')]
    failures = [c['case'] for c in cases if c.get('status') == 'FAIL']
    for name in required & set(indexed):
        status = indexed[name].get('status')
        if status != 'PASS' and not (name.endswith('-pagination') and status == 'NOT_APPLICABLE_EXHAUSTED'):
            if name not in failures and name not in incomplete:
                incomplete.append(name)
    exemptions = [c['case'] for c in cases if c['case'] in optional and c.get('status') != 'PASS']
    counts = {}
    for c in cases:
        counts[c.get('status', 'UNKNOWN')] = counts.get(c.get('status', 'UNKNOWN'), 0) + 1
    return {'passed': not failures and not incomplete, 'requiredMissing': missing,
            'failedCases': failures, 'incompleteCases': incomplete,
            'optionalUnexercisedCases': exemptions, 'coveragePartial': bool(exemptions or incomplete),
            'caseStatusCounts': counts}


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


    def props(self, target, names):
        return self.call('qml-get', {'object': target, 'props': names})['props']

    def tree(self, root=None):
        payload = {'maxItems': 1800}
        if root:
            payload['root'] = root
        rows = []
        for _ in range(6):
            reply = self.call('dump-ui', payload)
            rows.extend(reply.get('items', []))
            if not reply.get('truncated'):
                return rows
            payload.update(cursor=reply['continuation']['cursor'], generation=reply['generation'])
        raise RuntimeError('UI tree evidence limit exceeded; completeness unverified')

    def visible_ref(self, name=None, class_prefix=None, allow_first=False):
        rows = self.call('ui-snapshot').get('elements', []) if name else self.tree()
        found = []
        for row in rows:
            if name and row.get('objectName') != name:
                continue
            if class_prefix and not row.get('class', '').startswith(class_prefix):
                continue
            q = self.call('ui-query', {'object': row['handle']})
            if positive_area(q):
                found.append((q['rect']['y'], row['handle']))
        if not found or (len(found) > 1 and not allow_first):
            raise RuntimeError('Missing/ambiguous visible item: ' + str(name or class_prefix))
        return sorted(found)[0][1]

    def ensure_world(self, world):
        for _ in range(3):
            try:
                ref = self.visible_ref('modePill_' + world)
                self.call('ui-click', {'target': ref})
                time.sleep(0.2)
                return
            except RuntimeError:
                self.back()
        raise RuntimeError('Cannot return to world navigation: ' + world)

    def back(self):
        self.call('ui-keypress', {'key': 'Escape'})
        time.sleep(0.35)

    def case(self, name, operation):
        begin = time.monotonic_ns()
        try:
            result = operation()
        except Exception as exc:
            result = {'status': 'BLOCKED', 'error': str(exc)}
        result.update(case=name, elapsedObservationMs=(time.monotonic_ns() - begin) / 1e6)
        write(self.output / ('deep-' + name + '.json'), result)
        return result

    def cache_entry(self, filename, predicate, seconds=30):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            for path in self.profile.rglob(filename):
                try:
                    doc = json.loads(path.read_text())
                    entries = doc.get('entries', {})
                    iterator = entries.items() if isinstance(entries, dict) else enumerate(entries)
                    for key, entry in iterator:
                        if predicate(entry):
                            return str(key), entry, doc.get('publication') or doc.get('revision')
                except (OSError, ValueError):
                    pass
            time.sleep(0.25)
        return None, None, None

    def page_more(self, kind):
        self.ensure_world('Tankoban')
        self.click('tankobanDiscoverType_' + kind)
        before = self.props('tankobanDiscoverBrowser', ['automationItemCount', 'exhausted', 'pageFailed', 'loading'])
        if before.get('exhausted'):
            return {'status': 'NOT_APPLICABLE_EXHAUSTED', 'before': before}
        for _ in range(12):
            self.call('ui-scroll', {'target': self.visible_ref('tankobanWorldScroll'), 'dy': -960, 'xFraction': 0.92, 'yFraction': 0.85})
            time.sleep(0.25)
            after = self.props('tankobanDiscoverBrowser', ['automationItemCount', 'exhausted', 'pageFailed', 'loading'])
            if (after.get('automationItemCount', 0) or 0) > (before.get('automationItemCount', 0) or 0) or after.get('pageFailed'):
                break
        deadline = time.monotonic() + 30
        while after.get('loading') and time.monotonic() < deadline:
            time.sleep(0.5)
            after = self.props('tankobanDiscoverBrowser', ['automationItemCount', 'exhausted', 'pageFailed', 'loading'])
        self.grab('deep-' + kind + '-pagination', 'tankobanDiscoverBrowser')
        # Return to top using ordinary wheel events, never a contentY setter.
        for _ in range(12):
            self.call('ui-scroll', {'target': self.visible_ref('tankobanWorldScroll'), 'dy': 960, 'xFraction': 0.92, 'yFraction': 0.85})
        return {'status': 'PASS' if after.get('automationItemCount', 0) > before.get('automationItemCount', 0) and not after.get('pageFailed') else 'FAIL',
                'before': before, 'after': after, 'scope': 'One additional returned browse page, not entire catalogue'}

    def search(self, world, query, expect_empty=False):
        trace_start = len(traces(self.raw_stderr))
        self.ensure_world(world)
        self.click('topBarSearch_Tankoban' if world == 'Tankoban' else 'topBarSearch')
        ref = self.visible_ref('searchSurfaceInput')
        self.call('ui-click', {'target': ref})
        self.call('ui-keypress', {'key': 'Ctrl+A'})
        self.call('ui-keypress', {'key': 'Backspace'})
        self.call('ui-text-input', {'target': ref, 'text': query})
        surface = world.lower() + 'SearchSurface'
        deadline = time.monotonic() + 45
        state = {}
        while time.monotonic() < deadline:
            state = self.props(surface, ['resultCount', 'searching', 'searched', 'lastDispatchedQuery', 'searchError', 'showingNoResults', 'showingProviderError'])
            if state.get('lastDispatchedQuery') == query and state.get('searched') and not state.get('searching'):
                break
            time.sleep(0.4)
        snap = self.call('ui-snapshot')
        names = list(dict.fromkeys(row['objectName'] for row in snap.get('elements', []) if row.get('objectName', '').startswith('searchResult_')))
        self.grab('deep-search-' + world.lower() + '-' + re.sub('[^a-z0-9]', '-', query.lower())[:40], surface)
        dataset = 'mal' if world == 'Tankoban' else 'imdb'
        specifications = [('mal', [query.strip().lower(), 24, 'manga']), ('comics', [query.strip(), 30])] if world == 'Tankoban' else [('imdb', [query.strip().lower(), 40])]
        current_events = traces(self.raw_stderr)[trace_start:]
        correlations = []
        native_rows = []
        for ds, args in specifications:
            qkey = query_key('search', args)
            matching = [e for e in current_events if e.get('component') == 'remote' and e.get('dataset') == ds and e.get('queryKey') == qkey and e.get('event') == 'result_delivery_end']
            successful = any(e.get('status') == 'success' for e in matching)
            correlations.append({'dataset': ds, 'queryKey': qkey, 'args': args, 'freshDelivery': matching, 'success': successful})
            # Read only the exact keyed result, never another search entry.
            for path in self.profile.rglob(ds + '_api_v1.json'):
                doc = json.loads(path.read_text())
                entry = doc.get('entries', {}).get(qkey, {})
                for row in entry.get('result', []) if isinstance(entry.get('result'), list) else []:
                    native_rows.append({'dataset': ds, **{k: row.get(k) for k in ['id', 'tt', 'mal_id', 'gcdId', 'year', 'title', 'title_english', 'type', 'volumes']}})
        native = all(c['success'] for c in correlations)
        good = (state.get('lastDispatchedQuery') == query and state.get('searched') and not state.get('searching')
                and not state.get('searchError') and native
                and (state.get('resultCount') == 0 if expect_empty else (state.get('resultCount') or 0) > 0))
        return {'status': 'PASS' if good else 'FAIL', 'query': query, 'properties': state, 'resultControls': names,
                'nativeDataset': dataset, 'nativeSearchCorrelations': correlations, 'nativeResultProjection': native_rows, 'expectedEmpty': expect_empty}

    def manga_series(self, label):
        deadline = time.monotonic() + 45
        head = {}
        while time.monotonic() < deadline:
            try:
                head = self.props('tankobanSeriesMasthead', ['ready', 'resolvedMalId', 'displayTitle', 'hasShelf', 'primaryAction'])
            except RuntimeError:
                time.sleep(0.3)
                continue
            if positive_integer(head.get('resolvedMalId')) > 0:
                break
            time.sleep(0.3)
        mal_id = positive_integer(head.get('resolvedMalId'))
        key, entry, publication = self.cache_entry('tankoban_remote_v1.json', lambda e: e.get('malId') == mal_id)
        if entry is None:
            self.grab('deep-' + label, 'mangaSeriesPage')
            return {'status': 'BLOCKED', 'masthead': head, 'reason': 'No successful native Tankoban owner entry; shelf-less failure is not zero-volume success'}
        rows = entry.get('volumes', [])
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            shelf = self.props('tankobanShelfState', ['rowCount', 'coveredCount'])
            if shelf.get('rowCount') == len(rows):
                break
            time.sleep(0.25)
        page = self.props('mangaSeriesPage', ['chapterOnly', 'chapterMode', 'hasShelf', 'loading', 'errorMsg', 'errorText'])
        self.grab('deep-' + label + '-first', 'mangaSeriesPage')
        observed = {}
        # One normal wheel notch advances one volume. This never activates/acquires it.
        for _ in range(min(len(rows) + 2, 130)):
            nodes = self.tree('readingRoomLibrary')
            parents = {n['handle']: n.get('parentHandle') for n in nodes}
            cards = {n['handle']: n['objectName'].removeprefix('tankobanVolumeCard_') for n in nodes if n.get('objectName', '').startswith('tankobanVolumeCard_')}
            for handle, number in cards.items():
                observed[number] = self.props(handle, ['index', 'nameText', 'cardState', 'stateText'])
                observed[number]['imageStates'] = []
            for item in nodes:
                if 'Image' not in item.get('class', ''):
                    continue
                ancestor = item.get('parentHandle')
                while ancestor and ancestor not in cards:
                    ancestor = parents.get(ancestor)
                if ancestor in cards:
                    image = self.props(item['handle'], ['source', 'status'])
                    if str(image.get('source', '')).startswith(('http://', 'https://')):
                        observed[cards[ancestor]]['imageStates'].append(image)
            if len(observed) >= len(rows) or not rows:
                break
            self.call('ui-scroll', {'target': self.visible_ref('volumeFlow'), 'dy': -120})
            time.sleep(0.12)
        self.grab('deep-' + label, 'mangaSeriesPage')
        numbers = [str(v.get('number', '')) for v in rows]
        expected_names = {}
        for v in rows:
            n, name = str(v.get('number', '')), str(v.get('name', '')).strip()
            expected_names[n] = '' if re.fullmatch(r'vol(?:ume)?\.?\s*0*' + re.escape(n), name, re.I) else name
        numeric = [float(n) for n in numbers if re.fullmatch(r'\d+(?:\.\d+)?', n)]
        mismatches = [{'number': n, 'expected': expected_names.get(n), 'observed': v.get('nameText')}
                      for n, v in observed.items() if n in expected_names and v.get('nameText') != expected_names[n]]
        order_mismatches = [n for i, n in enumerate(numbers) if n in observed and observed[n].get('index') != i]
        tankoban_key = query_key('series', [mal_id])
        native_events = [e for e in traces(self.raw_stderr) if e.get('dataset') == 'tankoban' and e.get('queryKey') == tankoban_key and e.get('event') == 'result_delivery_end']
        native_completed = bool(entry.get('sourceHash')) and bool(publication) and any(e.get('status') == 'prepared' for e in native_events)
        matched = native_completed and head.get('ready') and not page.get('loading') and not page.get('errorMsg') and not page.get('errorText') and shelf.get('rowCount') == len(rows) and len(set(numbers)) == len(numbers) and not mismatches and not order_mismatches
        matched = matched and numeric == sorted(numeric) and (bool(rows) or (page.get('chapterOnly') and page.get('chapterMode') and not page.get('hasShelf')))
        complete_ui = set(numbers).issubset(observed)
        return {'status': 'PASS' if matched and complete_ui else 'INCOMPLETE' if matched else 'FAIL',
                'malId': mal_id, 'masthead': head, 'nativePublication': publication,
                'nativeSourceHash': entry.get('sourceHash'), 'nativeQueryKey': tankoban_key, 'nativeDeliveryEvents': native_events, 'seriesInfo': entry.get('seriesInfo'),
                'nativeReturnedCount': len(rows), 'nativeVolumes': rows,
                'missingNameCount': sum(not str(r.get('name', '')).strip() for r in rows),
                'missingCoverUrlCount': sum(not str(r.get('cover', '')).strip() for r in rows), 'displayedShelf': shelf,
                'pageState': page, 'observedVolumeCards': observed, 'allReturnedRowsObserved': complete_ui, 'observationScope': 'All returned delegate facts when traversal completes; first/final window screenshots and per-image Qt states are separate',
                'nameMismatches': mismatches, 'displayOrderMismatches': order_mismatches, 'numericOrderAscending': numeric == sorted(numeric),
                'numericOrderScope': 'Numeric tokens in returned order; nonnumeric extras retained',
                'countClass': 'zero' if not rows else 'single' if len(rows) == 1 else 'multi',
                'upstreamCompleteness': 'Not independently established; seriesInfo volumeCount and returned length reported separately',
                'factsProvenance': 'Native Tankoban catalogue; countBasis=bookwalker denotes ingested BookWalker-derived facts, not live BookWalker enumeration',
                'coverNote': 'coveredCount is URL presence, not decoded art; image states/probe and screenshots are separate'}

    def comics_series(self, label):
        deadline = time.monotonic() + 45
        root = None
        while time.monotonic() < deadline:
            try:
                root = self.visible_ref(class_prefix='ComicSeriesPage_')
                page = self.props(root, ['locgId', 'seriesTitle', 'loading', 'errorMsg', 'notAvailable', 'gcTag'])
                if not page.get('loading'):
                    break
            except RuntimeError:
                pass
            time.sleep(0.4)
        if root is None:
            raise RuntimeError('No visible curated Comics series page')
        locg = str(page.get('locgId', '')).removeprefix('locg:')
        key, entry, publication = self.cache_entry('comics_api_v1.json', lambda e: e.get('method') == 'curatedSeries' and str(e.get('result', {}).get('locgId', '')).removeprefix('locg:') == locg)
        if not entry:
            return {'status': 'BLOCKED', 'page': page, 'reason': 'No successful native curatedSeries owner result'}
        result = entry.get('result', {})
        editions = [{k: row.get(k) for k in ('chid', 'title', 'displayTitle', 'format', 'collects', 'cover')} for row in result.get('editions', [])]
        tree = self.tree(root)
        ledger = next((r for r in tree if r.get('class', '').startswith('ComicDbLedger_')), None)
        if not ledger and editions:
            return {'status': 'FAIL', 'page': page, 'nativeEditions': editions, 'reason': 'Returned editions but no ComicDbLedger'}
        observed = []
        if ledger:
            nodes = self.tree(ledger['handle'])
            # Ledger->Column->format Column->edition Item; Repeater creates siblings.
            for row in nodes:
                if row.get('depth') != 3 or 'Item' not in row.get('class', ''):
                    continue
                props = self.props(row['handle'], ['chId', 'hasSource', 'dlState'])
                if props.get('chId') is None:
                    continue
                descendants = {row['handle']}
                for item in nodes:
                    if item.get('parentHandle') in descendants:
                        descendants.add(item['handle'])
                texts, images = [], []
                for item in nodes:
                    if item['handle'] not in descendants:
                        continue
                    if 'Text' in item.get('class', ''):
                        t = self.props(item['handle'], ['text'])
                        if t.get('text'):
                            texts.append(t['text'])
                    if 'Image' in item.get('class', ''):
                        images.append(self.props(item['handle'], ['source', 'status', 'rung']))
                observed.append({'chId': props['chId'], 'state': props, 'titleText': texts[0] if texts else '', 'images': [im for im in images if str(im.get('source', '')).startswith(('https://', 'http://'))][:2]})
        # Fresh root handle after tree epoch changes.
        root = self.visible_ref(class_prefix='ComicSeriesPage_')
        self.grab('deep-' + label, root)
        format_order = ['Compendium', 'Omnibus', 'Complete Library', 'Ultimate Collection', 'Epic Collection', 'Absolute', 'Deluxe', 'Hardcover', 'Trade Paperback', 'Graphic Novel', 'Other']
        formats = list(dict.fromkeys((e.get('format') or 'Other') for e in editions))
        ordered_formats = [f for f in format_order if f in formats] + [f for f in formats if f not in format_order]
        expected = [e for f in ordered_formats for e in editions if (e.get('format') or 'Other') == f]
        ids = [str(e.get('chid') or '') for e in expected]
        got = [str(e['chId']) for e in observed]
        title_mismatches = []
        for e, ui in zip(expected, observed):
            title = str(e.get('displayTitle') or e.get('title') or '')
            prefix = str(page.get('seriesTitle') or '')
            if prefix and title.lower().startswith(prefix.lower()):
                rest = re.sub(r'^[\s:,./–—\-]+', '', title[len(prefix):])
                if len(rest) >= 2:
                    title = rest
            if title != ui['titleText']:
                title_mismatches.append({'id': e.get('chid'), 'expected': title, 'observed': ui['titleText']})
        native_events = [e for e in traces(self.raw_stderr) if e.get('dataset') == 'comics' and e.get('queryKey') == key]
        accepted = native_success(native_events, 'comics', 'curatedSeries') and not page.get('loading') and not page.get('errorMsg')
        return {'status': 'PASS' if accepted and len(observed) == len(editions) and ids == got and not title_mismatches else 'FAIL',
                'page': page, 'nativeQueryKey': key, 'publication': publication, 'nativeOwnerSuccess': accepted,
                'nativeReturnedCount': len(editions), 'nativeEditions': editions, 'observedEditionRows': observed,
                'displayedCount': len(observed), 'expectedDisplayOrder': ids, 'observedDisplayOrder': got, 'titleMismatches': title_mismatches, 'nativeCoverageAggregate': result.get('coverage'),
                'countClass': 'zero' if not editions else 'single' if len(editions) == 1 else 'multi',
                'orderingExpected': 'Fixed format-group order, then native edition order within each group; natural numeric sorting is not implemented',
                'canonicalIndividualIssues': 'NOT_VERIFIABLE: this UI/API exposes edition rows and aggregate issue coverage, not individual canonical issue list'}

    def episode_window(self):
        nodes = self.tree('theatreEpisodeVirtualSpace')
        parents = {r['handle']: r.get('parentHandle') for r in nodes}
        roots = {}
        for row in nodes:
            if row.get('depth') == 1 and 'Item' in row.get('class', ''):
                p = self.props(row['handle'], ['absoluteIndex'])
                if p.get('absoluteIndex') is not None:
                    roots[row['handle']] = {'index': p['absoluteIndex'], 'identifier': None, 'titleTextCandidates': [], 'images': []}
        for row in nodes:
            ancestor = row['handle']
            while ancestor and ancestor not in roots:
                ancestor = parents.get(ancestor)
            if ancestor not in roots:
                continue
            record = roots[ancestor]
            name = row.get('objectName', '')
            if name.startswith('theatreEpisodePlay_'):
                record['identifier'] = name.removeprefix('theatreEpisodePlay_')
            if 'Text' in row.get('class', '') and len(record['titleTextCandidates']) < 4:
                value = self.props(row['handle'], ['text']).get('text')
                if value and len(str(value)) <= 200:
                    record['titleTextCandidates'].append(value)
            if 'Image' in row.get('class', ''):
                value = self.props(row['handle'], ['source', 'status'])
                if str(value.get('source', '')).startswith(('https://', 'http://')):
                    record['images'].append(value)
        return list(roots.values())

    def audit_season(self):
        state = self.props('theatreSeriesPage', ['activeSeason', 'episodeContentHeight', 'compactEpisodeRowHeight', 'nextUpEpisodeRowHeight', 'nextUpEpisodeIndex'])
        height = state.get('compactEpisodeRowHeight') or 0
        expected = 0
        if height:
            extra = (state.get('nextUpEpisodeRowHeight', height) - height) if state.get('nextUpEpisodeIndex', -1) >= 0 else 0
            expected = (state.get('episodeContentHeight', 0) - extra) / height
        observed = {}
        for _ in range(20):
            for row in self.episode_window():
                observed[row['index']] = row
            if len(observed) >= expected or not expected:
                break
            self.call('ui-scroll', {'target': self.visible_ref('theatreSeriesScroll'), 'dy': -360, 'xFraction': 0.92, 'yFraction': 0.8})
            time.sleep(0.15)
        return {'activeSeason': state.get('activeSeason'), 'derivedReturnedEpisodeCount': expected,
                'observedEpisodeRows': [observed[i] for i in sorted(observed)],
                'allReturnedEpisodeRowsObserved': expected > 0 and len(observed) == expected,
                'countBasis': 'Exact source row-height layout formula, not independent upstream episode total'}

    def switch_season_two(self):
        # Source-backed legacy tabs are unnamed. Locate actual rendered Season 2
        # text and click its scene center; parent MouseArea handles normal input.
        for _ in range(10):
            nodes = self.tree('theatreSeriesPage')
            for row in nodes:
                if 'Text' not in row.get('class', '') or not row.get('visible'):
                    continue
                if self.props(row['handle'], ['text']).get('text') == 'Season 2':
                    if positive_area(self.call('ui-query', {'object': row['handle']})):
                        self.call('ui-click', {'target': row['handle']})
                        deadline = time.monotonic() + 5
                        while time.monotonic() < deadline:
                            if self.props('theatreSeriesPage', ['activeSeason']).get('activeSeason') == 2:
                                return True
                            time.sleep(0.2)
            self.call('ui-scroll', {'target': self.visible_ref('theatreSeriesScroll'), 'dy': 360, 'xFraction': 0.92, 'yFraction': 0.8})
        return False

    def theatre_detail(self, label, expected_id, expected_type):
        deadline = time.monotonic() + 45
        props = {}
        fields = ['loading', 'errorMsg', 'title', 'mediaType', 'requestedSourceId', 'resolvedId', 'year', 'genresLine', 'rating', 'runtime', 'synopsis',
                  'activeSeason', 'episodeContentHeight', 'compactEpisodeRowHeight', 'nextUpEpisodeRowHeight', 'nextUpEpisodeIndex', 'episodeWindowStart', 'episodeWindowEnd', 'liveEpisodeDelegateCount']
        while time.monotonic() < deadline:
            try:
                props = self.props('theatreSeriesPage', fields)
            except RuntimeError:
                time.sleep(0.3)
                continue
            if props.get('resolvedId') and props.get('title') and not props.get('loading'):
                break
            time.sleep(0.4)
        self.grab('deep-' + label, 'theatreSeriesPage')
        denominator = props.get('compactEpisodeRowHeight') or 0
        count = None
        if denominator and props.get('episodeContentHeight') is not None:
            extra = (props.get('nextUpEpisodeRowHeight', denominator) - denominator) if (props.get('nextUpEpisodeIndex') or 0) >= 0 else 0
            count = (props['episodeContentHeight'] - extra) / denominator
        # Real scroll only; never activate a Play/Watch/Download control.
        self.call('ui-scroll', {'target': self.visible_ref('theatreSeriesScroll'), 'dy': -600, 'xFraction': 0.92, 'yFraction': 0.75})
        snap = self.call('ui-snapshot')
        episodes = [r['objectName'] for r in snap.get('elements', []) if r.get('objectName', '').startswith('theatreEpisodePlay_')]
        self.grab('deep-' + label + '-metadata', 'theatreSeriesPage')
        seasons = []
        switched = None
        if expected_type == 'series':
            seasons.append(self.audit_season())
            switched = self.switch_season_two()
            if switched:
                seasons.append(self.audit_season())
                self.grab('deep-' + label + '-season2', 'theatreSeriesPage')
        season_pass = expected_type != 'series' or (switched and all(s['allReturnedEpisodeRowsObserved'] for s in seasons))
        return {'status': 'PASS' if season_pass and props.get('resolvedId') == expected_id and props.get('mediaType') == expected_type and props.get('title') and (props.get('synopsis') or props.get('runtime') or props.get('genresLine')) and not props.get('loading') and not props.get('errorMsg') else 'FAIL',
                'properties': props, 'expectedId': expected_id, 'expectedMediaType': expected_type, 'derivedActiveSeasonEpisodeCount': count, 'observedEpisodeIdentifiers': episodes,
                'provenance': 'Native IMDb search/catalogue identity; normal Cinemeta public detail metadata and automatic related metadata',
                'playback': 'NOT_RUN', 'seasonSwitchTo2': switched, 'seasonAudits': seasons}

    def gcd_search_releases(self):
        search = self.search('Tankoban', 'Batman')
        candidates = [r for r in search['nativeResultProjection'] if r.get('dataset') == 'comics' and r.get('gcdId')]
        if search['status'] != 'PASS' or not candidates:
            self.back()
            return {'status': 'BLOCKED', 'search': search, 'reason': 'No successful current native Comics search result'}
        chosen = None
        for row in candidates:
            label = str(row['title']) + (' (' + str(row['year']) + ')' if row.get('year') else '')
            if 'searchResult_' + label in search['resultControls']:
                chosen = row
                self.call('ui-click', {'target': self.visible_ref('searchResult_' + label, allow_first=True)})
                break
        if not chosen:
            self.back()
            return {'status': 'BLOCKED', 'search': search, 'reason': 'No visible named GCD search tile for returned records'}
        try:
            deadline = time.monotonic() + 45
            root = None
            while time.monotonic() < deadline:
                try:
                    root = self.visible_ref(class_prefix='ComicSeries_')
                    page = self.props(root, ['gcdId', 'seriesTitle', 'totalReleases', 'loading', 'errorMsg', 'sortMode'])
                    if page.get('gcdId') == chosen['gcdId'] and not page.get('loading'):
                        break
                except RuntimeError:
                    pass
                time.sleep(0.3)
            if not root:
                raise RuntimeError('GCD native search did not open ComicSeries')
            key = query_key('downloadsFor', [chosen['gcdId']])
            entry = None
            deadline = time.monotonic() + 20
            while time.monotonic() < deadline and entry is None:
                for path in self.profile.rglob('comics_api_v1.json'):
                    doc = json.loads(path.read_text())
                    entry = doc.get('entries', {}).get(key)
                if entry is None:
                    time.sleep(0.25)
            if entry is None:
                return {'status': 'BLOCKED', 'page': page, 'search': search, 'reason': 'No native downloadsFor owner entry'}
            raw_rows = entry.get('result', [])
            projection = [{k: r.get(k) for k in ('postId', 'title', 'date', 'kind')} for r in raw_rows]
            nodes = self.tree(root)
            rows = []
            for node in nodes:
                if node.get('objectName', '').startswith('comicReleaseRow_'):
                    values = self.props(node['handle'], ['relId', 'dlState'])
                    descendants = {node['handle']}
                    for descendant in nodes:
                        if descendant.get('parentHandle') in descendants:
                            descendants.add(descendant['handle'])
                    values['images'] = []
                    values['nameText'] = ''
                    for descendant in nodes:
                        if descendant['handle'] not in descendants:
                            continue
                        if 'Image' in descendant.get('class', ''):
                            image = self.props(descendant['handle'], ['source', 'status'])
                            if str(image.get('source', '')).startswith(('http://', 'https://')):
                                values['images'].append(image)
                        if not values['nameText'] and 'Text' in descendant.get('class', ''):
                            text = self.props(descendant['handle'], ['text']).get('text')
                            if text and text != '#':
                                values['nameText'] = text
                    rows.append(values)
            current_events = traces(self.raw_stderr)
            delivery = [e for e in current_events if e.get('component') == 'remote' and e.get('dataset') == 'comics'
                        and e.get('queryKey') == key and e.get('event') == 'result_delivery_end']
            ordered = [r for r in projection if r.get('kind') != 'single'] + [r for r in projection if r.get('kind') == 'single']
            expected = [str(r['postId']) for r in ordered]
            observed = [str(r['relId']) for r in rows]
            names = {str(r['postId']): r.get('title') for r in projection}
            title_mismatches = [{'id': r['relId'], 'expected': names.get(str(r['relId'])), 'observed': r['nameText']} for r in rows if names.get(str(r['relId'])) != r['nameText']]
            self.grab('deep-comics-gcd-search-releases', self.visible_ref(class_prefix='ComicSeries_'))
            return {'status': 'PASS' if expected == observed and not title_mismatches and page.get('totalReleases') == len(projection)
                    and page.get('gcdId') == chosen['gcdId'] and not page.get('loading') and not page.get('errorMsg')
                    and any(e.get('status') == 'success' for e in delivery) else 'FAIL',
                    'search': search, 'selectedNativeSearchRow': chosen, 'page': page, 'nativeQueryKey': key,
                    'nativeDeliveryEvents': delivery, 'nativeReturnedCount': len(projection),
                    'releaseProjection': projection, 'displayedReleaseRows': rows, 'expectedDisplayOrder': expected, 'observedDisplayOrder': observed, 'titleMismatches': title_mismatches,
                    'scope': 'GCD-associated release posts/collections in the same native comics dataset; not canonical individual issue enumeration',
                    'sourceAcquisition': 'NOT_RUN; normal automatic public cover metadata enrichment may occur'}
        finally:
            self.back()

    def deep_suite(self, landing):
        cases = []
        comics = next(x for x in landing if x['name'] == 'comics')
        manga = next(x for x in landing if x['name'] == 'manga')
        for index, card in enumerate(comics['visibleCards'][:3]):
            def run_comic(card=card, index=index):
                self.ensure_world('Tankoban')
                self.click('tankobanDiscoverType_comics')
                self.click(card['objectName'])
                try:
                    return self.comics_series('comics-series-' + str(index + 1))
                finally:
                    self.back()
            cases.append(self.case('comics-series-' + str(index + 1), run_comic))
        if comics['visibleCards']:
            def reopen_comic():
                self.ensure_world('Tankoban')
                self.click('tankobanDiscoverType_comics')
                self.click(comics['visibleCards'][0]['objectName'])
                try:
                    result = self.comics_series('comics-warm-reopen')
                    result['cacheScope'] = 'Same-process warm Back/reopen, not forced network refetch'
                    return result
                finally:
                    self.back()
            cases.append(self.case('comics-warm-reopen', reopen_comic))
        cases.append(self.case('comics-pagination', lambda: self.page_more('comics')))
        cases.append(self.case('comics-gcd-search-releases', self.gcd_search_releases))
        if manga['visibleCards']:
            card = manga['visibleCards'][0]
            def first_manga():
                self.ensure_world('Tankoban')
                self.click('tankobanDiscoverType_manga')
                self.click(card['objectName'])
                try:
                    return self.manga_series('manga-series-1')
                finally:
                    self.back()
            cases.append(self.case('manga-series-1', first_manga))
        for index, query in enumerate(['Goodbye Eri', 'One Piece'], 2):
            def searched_series(query=query, index=index):
                search = self.search('Tankoban', query)
                if search['status'] != 'PASS' or not search['resultControls']:
                    self.back()
                    return {'status': 'BLOCKED', 'search': search}
                native_candidates = [r for r in search['nativeResultProjection'] if r.get('dataset') == 'mal' and r.get('mal_id') and 'searchResult_' + str(r.get('title_english') or r.get('title')) in search['resultControls']]
                if not native_candidates:
                    self.back()
                    return {'status': 'BLOCKED', 'search': search, 'reason': 'No ID-correlated native MAL search control'}
                chosen = native_candidates[0]
                name = 'searchResult_' + str(chosen.get('title_english') or chosen.get('title'))
                self.call('ui-click', {'target': self.visible_ref(name, allow_first=True)})
                try:
                    result = self.manga_series('manga-series-' + str(index))
                    result['search'] = search
                    result['expectedMalIdFromNativeSearch'] = chosen['mal_id']
                    if result.get('malId') != chosen['mal_id']:
                        result['status'] = 'FAIL'
                        result['identityMismatch'] = True
                    return result
                finally:
                    self.back()
            cases.append(self.case('manga-series-' + str(index), searched_series))
        if manga['visibleCards']:
            def reopen_manga():
                self.ensure_world('Tankoban')
                self.click('tankobanDiscoverType_manga')
                self.click(manga['visibleCards'][0]['objectName'])
                try:
                    result = self.manga_series('manga-warm-reopen')
                    result['cacheScope'] = 'Same-process warm Back/reopen, not forced network refetch'
                    return result
                finally:
                    self.back()
            cases.append(self.case('manga-warm-reopen', reopen_manga))
        cases.append(self.case('manga-pagination', lambda: self.page_more('manga')))
        for world in ['Tankoban', 'Theatre']:
            def empty_search(world=world):
                result = self.search(world, 'zzqv983761catalogueunmatched', True)
                self.back()
                return result
            cases.append(self.case(world.lower() + '-empty-search', empty_search))
        for kind, query in [('movie', 'Inception'), ('show', 'Breaking Bad')]:
            def detail(query=query, kind=kind):
                search = self.search('Theatre', query)
                if search['status'] != 'PASS' or not search['resultControls']:
                    self.back()
                    return {'status': 'BLOCKED', 'search': search}
                self.call('ui-click', {'target': self.visible_ref(search['resultControls'][0], allow_first=True)})
                try:
                    result = self.theatre_detail('theatre-' + kind, search['resultControls'][0].removeprefix('searchResult_'), 'movie' if kind == 'movie' else 'series')
                    result['search'] = search
                    return result
                finally:
                    self.back()
            cases.append(self.case('theatre-' + kind, detail))
        cases += [{'case': 'forced-error-recovery', 'status': 'NOT_RUN', 'reason': 'No error injection/network override permitted; only genuine observed errors recorded'},
                  {'case': 'forced-reload', 'status': 'NOT_SUPPORTED_UI', 'reason': 'No ordinary reload control; warm Back/reopen must not be called forced refetch'},
                  {'case': 'all-catalogue-records', 'status': 'OUT_OF_SCOPE', 'reason': 'Representative series and all their returned rows, not exhaustive upstream catalogue'}]
        for catalogue in ('manga', 'comics'):
            classes = sorted({r.get('countClass') for r in cases if r.get('case', '').startswith(catalogue + '-series') and r.get('countClass')})
            missing = sorted(set(('zero', 'single', 'multi')) - set(classes))
            cases.append({'case': catalogue + '-representative-classes', 'status': 'INCOMPLETE' if missing else 'PASS',
                          'observedClasses': classes, 'missingClasses': missing,
                          'meaning': 'Coverage gap in bounded current sample, not proof of product failure'})
        write(self.output / 'deep-coverage.json', cases)
        return cases


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
    assert query_key('search', ['Batman', 30]) != query_key('search', ['batman', 30])
    assert query_key('series', [13]) != query_key('series', ['13'])
    assert assess_deep([])['passed'] is False
    assert assess_deep([{'case': 'manga-series-1', 'status': 'NOT_RUN'}])['passed'] is False
    assert 'manga-series-1' in assess_deep([{'case': 'manga-series-1', 'status': 'INCOMPLETE'}])['incompleteCases']
    assert 'manga-series-1' in assess_deep([{'case': 'manga-series-1', 'status': 'FAIL'}])['failedCases']
    all_required = assess_deep([])['requiredMissing']
    passing = [{'case': name, 'status': 'PASS'} for name in all_required]
    assert assess_deep(passing)['passed']
    assert assess_deep(passing + [{'case': 'forced-reload', 'status': 'NOT_SUPPORTED_UI'}])['passed']
    cover_case = {'case': 'manga-series-1', 'nativeVolumes': [{'number': '1', 'cover': 'https://example.test/cover'}], 'observedVolumeCards': {}}
    assert deep_cover_assessment([cover_case], [])['status'] == 'INCOMPLETE'
    cover_case['observedVolumeCards'] = {'1': {'imageStates': [{'source': 'https://example.test/cover', 'status': 3}]}}
    assert deep_cover_assessment([cover_case], [])['status'] == 'FAIL'
    cover_case['observedVolumeCards']['1']['imageStates'][0]['status'] = 1
    assert deep_cover_assessment([cover_case], [])['status'] == 'PASS'
    cover_case['nativeVolumes'][0]['cover'] = ''
    cover_case['observedVolumeCards'] = {}
    assert deep_cover_assessment([cover_case], [])['missingOptionalFields']
    cover_case['nativeVolumes'][0]['cover'] = 'https://example.test/cover'
    cover_case['observedVolumeCards'] = {'1': {'imageStates': [{'source': 'qrc:/placeholder.svg', 'status': 1}]}}
    assert deep_cover_assessment([cover_case], [])['status'] == 'INCOMPLETE'
    assert positive_integer('13') == 13
    assert positive_integer('malformed') == 0
    assert positive_integer(None) == 0
    assert positive_integer('-1') == 0
    print('25 pure qualification helper checks passed')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary', type=Path)
    p.add_argument('--lanista', type=Path, help='Record companion identity; IPC uses its published JSON protocol')
    p.add_argument('--source', type=Path, default=Path.cwd())
    p.add_argument('--output', type=Path)
    p.add_argument('--startup-timeout', type=int, default=90)
    p.add_argument('--catalogue-timeout', type=int, default=60)
    p.add_argument('--poster-seconds', type=int, default=20)
    p.add_argument('--deep', action='store_true', help='Bounded real series/search/pagination qualification')
    p.add_argument('--compiled-provenance', type=Path, help='Verified prior binary provenance for QA-only reuse')
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
    compiled = json.loads(a.compiled_provenance.read_text()) if a.compiled_provenance else {}
    qa_head = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
    if compiled:
        for key, path in [('binarySha256', binary), ('lanistaSha256', a.lanista), ('qmlManifestSha256', manifest)]:
            if compiled.get(key) != sha(path):
                raise RuntimeError('Compiled provenance hash mismatch: ' + key)
        if compiled.get('qualificationSourceCommit') != qa_head or not compiled.get('compiledSourceCommit'):
            raise RuntimeError('Compiled/qualification revision provenance mismatch')
    receipt = {'configuredContentEndpoint': configured_endpoint or compiled.get('configuredContentEndpoint'),
               'compiledSourceCommit': compiled.get('compiledSourceCommit') or qa_head,
               'compiledProvenance': compiled,
               'qualificationSourceCommit': subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip(),
               'binarySha256': sha(binary), 'lanistaSha256': sha(a.lanista),
               'qmlManifestSha256': sha(manifest),
               'sourceCommit': compiled.get('compiledSourceCommit') or qa_head,
               'sourceStatus': subprocess.check_output(['git', '-C', str(source), 'status', '--porcelain'], text=True),
               'driverSha256': sha(__file__), 'profileInitiallyEmpty': True,
               'rendering': 'full native app under X11; CI supplies Xvfb, local validation supplies owned desktop',
               'proxyVariableNamesPresent': [k for k in env if k.lower() in ('http_proxy', 'https_proxy', 'all_proxy', 'no_proxy')],
               'systemProxyOptIn': env.get('COLOSSEUM_USE_SYSTEM_PROXY', ''),
               'coldDefinition': 'Empty local profile only; origin/CDN and OS cache state uncontrolled',
               'launchMonotonicNs': time.monotonic_ns(), 'launchEpochMs': time.time_ns() / 1e6}
    write(output / 'launch.json', receipt)
    results, error, driver, process = [], None, None, None
    deep_results = []
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
        if a.deep:
            deep_results = driver.deep_suite(results)
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
        key = (event.get('remoteId'), event.get('dataset'), event.get('queryKey'))
        if event.get('event') == 'enqueue':
            starts[key] = event
        elif event.get('event') == 'result_delivery_end' and key in starts:
            begin = starts[key]
            duration = event.get('monotonicMs', 0) - begin.get('monotonicMs', 0)
            deliveries.append({'dataset': event.get('dataset'), 'method': event.get('method'),
                               'queryKey': event.get('queryKey'), 'enqueueSourceGeneration': begin.get('sourceGeneration'), 'deliverySourceGeneration': event.get('sourceGeneration'), 'status': event.get('status'),
                               'reason': event.get('reason'), 'requestToDeliveryMs': duration,
                               'classification': 'native-data-delivered' if event.get('status') == 'success' else 'failure-latency',
                               'timingBasis': 'Same-process native monotonic enqueue to result_delivery_end; not server latency or visual paint'})
    write(output / 'native-delivery-timings.json', deliveries)
    probes = list(profile.rglob('poster-timing.json'))
    poster = {'probeAvailable': bool(probes), 'warning': 'Image decode/paint requires visible-card correlation; local SVG/resources excluded.'}
    rows = []
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
    if a.deep:
        deep_results.append(deep_cover_assessment(deep_results, rows))
        write(output / 'deep-coverage.json', deep_results)
    assessment = assess_deep(deep_results) if a.deep else {'passed': True, 'coveragePartial': False}
    deep_passed = assessment['passed']
    landing_passed = len(results) == 3 and all(r['passed'] for r in results)
    passed = deep_passed and landing_passed and not error and exit_code == 0 and not forced
    write(output / 'summary.json', {'cataloguesPassed': landing_passed, 'qualificationPassed': passed, 'journeys': results, 'error': error,
                                   'posters': poster, 'numericExitStatus': exit_code,
                                   'deepCases': deep_results, 'deepRequested': a.deep, 'deepPassed': deep_passed, 'deepAssessment': assessment,
                                   'warmRunPerformed': False, 'sameProcessWarmRevisitPerformed': a.deep,
                                   'timingWarning': 'Native trace success must be checked; failed/empty transfers and IPC timings are not loads. No server latency or exact visual paint duration is inferred.'})
    print(json.dumps(sanitize({'cataloguesPassed': landing_passed, 'qualificationPassed': passed, 'deepAssessment': assessment, 'error': error, 'numericExitStatus': exit_code})))
    return 0 if passed else 1


if __name__ == '__main__':
    sys.exit(main())
