#!/usr/bin/env python3
"""Actual-app addon installation gate. Run on an owned X11 desktop or Xvfb.

No direct owner mutation, model invocation, playback, download, or stream request.
A separate, initially empty profile is reused only for the persistence restart.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import uuid
from urllib.parse import urlsplit

from b1_native_catalogue_qualification import Driver, positive_area, preflight, sanitize, sha, wait_for_value, write

NYAA = 'colosseum.well.nyaa'
TORRENTIO = 'com.stremio.torrentio.addon'
MANIFEST = 'https://torrentio.strem.fun/manifest.json'


def valid_provenance(value):
    required = ('binarySha256', 'lanistaSha256', 'qmlManifestSha256', 'qualificationSourceCommit', 'compiledSourceCommit')
    return isinstance(value, dict) and all(isinstance(value.get(key), str) and value[key].strip() for key in required)


def project_owner(row):
    manifest = row.get('manifest', {})
    transport = row.get('transportUrl', '')
    u = urlsplit(transport)
    if u.username or u.password or u.query or u.fragment:
        raise RuntimeError('Configured or credential-bearing transport is outside this gate')
    return {'id': row.get('id'), 'enabled': row.get('enabled') is True,
            'core': row.get('core') is True, 'transport': transport,
            'manifestId': manifest.get('id'), 'name': manifest.get('name'), 'installedAt': row.get('installedAt'), 'manifestVersion': manifest.get('version'),
            'resources': manifest.get('resources', []), 'types': manifest.get('types', [])}


def valid_profile_id(value):
    if value in ('legacy', 'local'):
        return True
    if not isinstance(value, str):
        return False
    try:
        parsed = uuid.UUID(value)
        return parsed.int != 0 and str(parsed) == value
    except ValueError:
        return False


def owner_rows(app_data_root, active_profile_id):
    # ExtensionsStore combines global non-Theatre rows with the active profile's
    # Theatre rows. "legacy" is an active profile mode on a fresh no-account boot,
    # not evidence of an inactive copy. Never scan or merge other profile folders.
    if not valid_profile_id(active_profile_id):
        raise RuntimeError('Missing or unsafe active extension profile identity')
    root = Path(app_data_root).resolve()
    sources = ((root / 'extensions' / 'installed.json', NYAA, 'global'),
               (root / 'profiles' / active_profile_id / 'extensions' / 'installed.json', TORRENTIO, active_profile_id))
    result = []
    for path, identity, scope in sources:
        if path.resolve() != path or not path.resolve().is_relative_to(root):
            raise RuntimeError('Extension owner path leaves its exact declared scope')
        try:
            doc = json.loads(path.read_text())
        except (OSError, ValueError):
            continue
        if not isinstance(doc, dict) or not isinstance(doc.get('extensions'), list):
            raise RuntimeError('Malformed extension owner document')
        if scope != 'global' and (doc.get('version') != 1 or doc.get('profileId') != active_profile_id):
            raise RuntimeError('Theatre extension owner profile envelope mismatch')
        for row in doc['extensions']:
            if isinstance(row, dict) and row.get('id') == identity:
                projected = project_owner(row)
                projected['ownerFileRelative'] = str(path.relative_to(root))
                projected['ownerProfileId'] = scope
                result.append(projected)
    return result


def enabled_owner(rows, identity):
    return any(r.get('id') == identity and r.get('manifestId') == identity and r.get('enabled') for r in rows)


def persistence_pass(before, after):
    return all(enabled_owner(before, identity) and enabled_owner(after, identity)
               and any(a['id'] == identity and b['id'] == identity and a['transport'] == b['transport']
                       for a in before for b in after) for identity in (NYAA, TORRENTIO))


def manifest_refresh_proven(submitted, setup_open, rows, submitted_epoch):
    return submitted and setup_open is False and enabled_owner(rows, TORRENTIO) and any(
        r['id'] == TORRENTIO and r.get('transport') == MANIFEST
        and (r.get('installedAt') or 0) >= (submitted_epoch or 0) for r in rows)


class AddonDriver(Driver):
    def open_store(self):
        self.call('ui-keypress', {'key': 'Ctrl+Shift+E'})
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            try:
                self.visible_ref('extensionsStorePage')
                return
            except RuntimeError:
                time.sleep(0.3)
        raise RuntimeError('Extensions Store not visible')

    def nyaa(self, mutate):
        name = 'houseToggle_' + NYAA
        initial = self.props(name, ['checked'])
        if mutate and initial.get('checked') is False:
            self.click(name)
        state = wait_for_value(lambda: self.props(name, ['checked']) if self.props(name, ['checked']).get('checked') else None, 5)
        rows = owner_rows(self.app_data_root, self.active_profile_id)
        self.grab('nyaa-enabled' if mutate else 'nyaa-restarted', 'extensionsStorePage')
        return {'status': 'PASS' if state and enabled_owner(rows, NYAA) else 'FAIL',
                'initialUI': initial, 'finalUI': state, 'ownerRows': [r for r in rows if r['id'] == NYAA],
                'operation': 'Enable pre-seeded disabled House source' if mutate else 'Read persisted enabled state',
                'newManifestInstalled': False}

    def torrentio_card(self):
        self.click('extensionsSearchButton')
        ref = self.visible_ref('extensionsSearchField')
        self.call('ui-text-input', {'target': ref, 'text': 'Torrentio'})
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            snapshot = self.call('ui-snapshot')
            names = list(dict.fromkeys(r.get('objectName') for r in snapshot.get('elements', [])
                         if r.get('objectName', '').startswith('extensionsCard_')))
            for name in names:
                try:
                    ref = self.visible_ref(name, allow_first=True)
                    nodes = self.tree(ref)
                    titles = [self.props(n['handle'], ['text']).get('text') for n in nodes if 'Text' in n.get('class', '')]
                    if any(str(t).strip() == 'Torrentio' for t in titles):
                        return name
                except RuntimeError:
                    continue
            if self.props('extensionsStorePage', ['loadError']).get('loadError'):
                break
            time.sleep(0.4)
        raise RuntimeError('No visible exact Torrentio card in normal Store search')

    def torrentio(self, mutate):
        try:
            card = self.torrentio_card()
        except RuntimeError as exc:
            errors = self.props('extensionsStorePage', ['loadError', 'installError', 'loading'])
            self.grab('torrentio-store-blocked', 'extensionsStorePage')
            return {'status': 'BLOCKED', 'reason': str(exc), 'providerErrors': errors, 'manifestRequested': None,
                    'initialOwnerRows': [r for r in owner_rows(self.app_data_root, self.active_profile_id) if r['id'] == TORRENTIO]}
        initial = self.props(self.visible_ref(card, allow_first=True), ['installed', 'busy'])
        initial_owners = [r for r in owner_rows(self.app_data_root, self.active_profile_id) if r['id'] == TORRENTIO]
        submitted = False
        submitted_epoch = None
        if mutate:
            self.call('ui-click', {'target': self.visible_ref(card, allow_first=True)})
            # Normal setup can load its public configuration page. Use its explicit
            # paste-link field, never interact with provider credentials or Web DOM.
            deadline = time.monotonic() + 20
            typed = False
            while time.monotonic() < deadline:
                try:
                    nodes = self.tree('extensionsSetupSheet')
                    for node in nodes:
                        if not any(x in node.get('class', '') for x in ('TextField', 'TextInput')):
                            continue
                        props = self.props(node['handle'], ['placeholderText'])
                        if props.get('placeholderText') == 'Paste the add-on link the page gave you' and positive_area(self.call('ui-query', {'object': node['handle']})):
                            self.call('ui-text-input', {'target': node['handle'], 'text': MANIFEST})
                            submitted_epoch = int(time.time())
                            self.call('ui-keypress', {'key': 'Return'})
                            submitted = True
                            typed = True
                            break
                    if typed:
                        break
                except RuntimeError:
                    pass
                time.sleep(0.3)
            if not typed:
                raise RuntimeError('Torrentio setup did not expose its normal paste-link field')
        deadline = time.monotonic() + 45
        rows, error = None, {}
        while time.monotonic() < deadline:
            current = owner_rows(self.app_data_root, self.active_profile_id)
            error = self.props('extensionsStorePage', ['installError', 'loadError'])
            try:
                setup_state = self.props('extensionsSetupSheet', ['status', 'open'])
                error['setupStatus'] = setup_state.get('status', '')
                setup_open = setup_state.get('open')
            except RuntimeError:
                error['setupStatus'] = ''
                setup_open = None
            refreshed = manifest_refresh_proven(submitted, setup_open, current, submitted_epoch)
            if enabled_owner(current, TORRENTIO) and (not mutate or (submitted and refreshed)):
                rows = current
                break
            if error.get('installError') or error.get('loadError') or "Couldn't add it:" in str(error.get('setupStatus', '')):
                break
            time.sleep(0.2)
        state = self.props(card, ['installed', 'busy'])
        self.grab('torrentio-installed' if mutate else 'torrentio-restarted', 'extensionsStorePage')
        return {'status': 'PASS' if rows and state.get('installed') and not error.get('installError') and not error.get('loadError') and "Couldn't add it:" not in str(error.get('setupStatus', '')) else 'FAIL',
                'manifestRequested': MANIFEST if submitted else None, 'manifestSubmissionEpoch': submitted_epoch, 'setupOpenAfterObservation': setup_open, 'initialOwnerRows': initial_owners, 'initialUI': initial, 'finalUI': state,
                'ownerRows': [r for r in (rows or []) if r['id'] == TORRENTIO], 'providerErrors': error,
                'operation': ('Normal UI manifest refresh of pre-seeded entry' if initial_owners else 'Normal UI new manifest installation') if mutate else 'Read persisted installation'}



def self_test():
    n = {'id': NYAA, 'manifestId': NYAA, 'enabled': True, 'transport': 'colosseum://well/nyaa'}
    t = {'id': TORRENTIO, 'manifestId': TORRENTIO, 'enabled': True, 'transport': MANIFEST}
    assert persistence_pass([n, t], [n, t])
    assert not persistence_pass([n, t], [n])
    assert not enabled_owner([dict(n, enabled=False)], NYAA)
    assert not enabled_owner([dict(t, manifestId='wrong')], TORRENTIO)
    assert not persistence_pass([n, t], [n, dict(t, transport='https://example.test/manifest.json')])
    assert project_owner({'id': NYAA, 'enabled': True, 'manifest': {}})['id'] == NYAA
    try:
        project_owner({'transportUrl': 'https://user:secret@example.test/manifest.json'})
        raise AssertionError('Credentials accepted')
    except RuntimeError:
        pass
    assert sanitize({'token': 'secret'}) == {'token': '[redacted]'}
    assert not persistence_pass([dict(n, enabled=False), dict(t, enabled=False)], [n, t])
    assert not persistence_pass([n, t], [n, dict(t, enabled=False)])
    assert enabled_owner([t], TORRENTIO) and not enabled_owner([dict(t, enabled=False)], TORRENTIO)
    seeded = [dict(t, installedAt=100)]
    assert not manifest_refresh_proven(False, False, seeded, 100)
    assert not manifest_refresh_proven(True, True, seeded, 100)
    assert manifest_refresh_proven(True, False, seeded, 100)
    assert not manifest_refresh_proven(True, False, [dict(t, installedAt=99)], 100)
    assert not valid_provenance({})
    assert not valid_provenance([])
    assert not valid_provenance({'compiledSourceCommit': 'known'})
    assert valid_provenance({key: 'verified' for key in ('binarySha256', 'lanistaSha256', 'qmlManifestSha256', 'qualificationSourceCommit', 'compiledSourceCommit')})
    assert valid_profile_id('legacy') and valid_profile_id('local')
    assert valid_profile_id('12345678-1234-1234-1234-123456789abc')
    assert all(not valid_profile_id(v) for v in ('', 'sealed', '../legacy', '/tmp', 'LEGACY', None, '00000000-0000-0000-0000-000000000000'))
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        global_file = root / 'extensions' / 'installed.json'
        active_file = root / 'profiles' / 'legacy' / 'extensions' / 'installed.json'
        inactive_file = root / 'profiles' / 'local' / 'extensions' / 'installed.json'
        for path in (global_file, active_file, inactive_file):
            path.parent.mkdir(parents=True, exist_ok=True)
        nyaa = {'id': NYAA, 'enabled': True, 'transportUrl': 'colosseum://well/nyaa', 'manifest': {'id': NYAA}}
        torrentio = {'id': TORRENTIO, 'enabled': True, 'transportUrl': MANIFEST, 'manifest': {'id': TORRENTIO}}
        global_file.write_text(json.dumps({'extensions': [nyaa, torrentio]}))
        inactive_file.write_text(json.dumps({'version': 1, 'profileId': 'local', 'extensions': [torrentio]}))
        assert not enabled_owner(owner_rows(root, 'legacy'), TORRENTIO)
        active_file.write_text(json.dumps({'version': 1, 'profileId': 'legacy', 'extensions': [dict(torrentio, enabled=False)]}))
        assert not enabled_owner(owner_rows(root, 'legacy'), TORRENTIO)
        active_file.write_text(json.dumps({'version': 1, 'profileId': 'legacy', 'extensions': [torrentio]}))
        rows = owner_rows(root, 'legacy')
        assert len(rows) == 2 and enabled_owner(rows, NYAA) and enabled_owner(rows, TORRENTIO)
        assert next(r for r in rows if r['id'] == TORRENTIO)['ownerFileRelative'] == 'profiles/legacy/extensions/installed.json'
        assert persistence_pass(rows, owner_rows(root, 'legacy'))
        active_file.write_text(json.dumps({'version': 1, 'profileId': 'local', 'extensions': [torrentio]}))
        try:
            owner_rows(root, 'legacy')
            raise AssertionError('Mismatched profile envelope accepted')
        except RuntimeError:
            pass
        try:
            owner_rows(root, '../legacy')
            raise AssertionError('Unsafe profile identity accepted')
        except RuntimeError:
            pass
    print('29 addon qualification helper checks passed')



def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path)
    parser.add_argument('--lanista', type=Path)
    parser.add_argument('--source', type=Path, default=Path.cwd())
    parser.add_argument('--output', type=Path)
    parser.add_argument('--self-test', action='store_true')
    parser.add_argument('--compiled-provenance', type=Path, help='Verified compiled binary provenance when checkout and executable differ')
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if not args.binary or not args.lanista or not args.output or not args.compiled_provenance:
        parser.error('--binary --lanista --output --compiled-provenance required')
    source, binary, output = args.source.resolve(), args.binary.resolve(), args.output.resolve()
    if output.exists():
        raise RuntimeError('Use a new output directory')
    preflight(source, binary)
    manifest = binary.parent / 'qml-build.manifest'
    if not manifest.is_file():
        raise RuntimeError('Production QML manifest missing')
    output.mkdir(parents=True)
    private = Path(tempfile.mkdtemp(prefix='b1-addon-private-', dir=output.parent))
    profile = private / 'profile'
    for name in ('data', 'cache', 'config'):
        (profile / name).mkdir(parents=True)
    tag = 'b1-addons-' + uuid.uuid4().hex[:12]
    env = {k: v for k, v in os.environ.items() if not any(x in k.upper() for x in ('TOKEN', 'PASSWORD', 'SECRET', 'API_KEY'))}
    env.update(XDG_DATA_HOME=str(profile / 'data'), XDG_CACHE_HOME=str(profile / 'cache'), XDG_CONFIG_HOME=str(profile / 'config'),
               COLOSSEUM_APPDATA_TAG=tag, COLOSSEUM_LANISTA_PIPE=tag, COLOSSEUM_LANISTA_DRIVE='1',
               COLOSSEUM_AUTOMATION_FOCUS_SEAMS='0', COLOSSEUM_CATALOGUE_TRACE='1', QT_QPA_PLATFORM='xcb')
    qa_head = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
    source_status = subprocess.check_output(['git', '-C', str(source), 'status', '--porcelain'], text=True)
    compiled = json.loads(args.compiled_provenance.read_text()) if args.compiled_provenance else {}
    if not valid_provenance(compiled):
        raise RuntimeError('Nonempty complete compiled provenance object required')
    if compiled:
        for key, path in [('binarySha256', binary), ('lanistaSha256', args.lanista), ('qmlManifestSha256', manifest)]:
            if compiled.get(key) != sha(path):
                raise RuntimeError('Compiled provenance mismatch: ' + key)
        if compiled.get('qualificationSourceCommit') != qa_head or not compiled.get('compiledSourceCommit'):
            raise RuntimeError('Compiled/qualification commit provenance mismatch')
    write(output / 'launch.json', {'binarySha256': sha(binary), 'lanistaSha256': sha(args.lanista),
          'qmlManifestSha256': sha(manifest), 'driverSha256': sha(__file__),
          'helperDriverSha256': sha(Path(__file__).with_name('b1_native_catalogue_qualification.py')),
          'qualificationSourceCommit': qa_head, 'sourceStatus': source_status,
          'compiledSourceCommit': compiled.get('compiledSourceCommit'), 'compiledProvenance': compiled,
          'compiledNativeSourceCommit': compiled.get('compiledNativeSourceCommit') or compiled.get('compiledSourceCommit'),
          'runtimeQmlSourceCommit': compiled.get('runtimeQmlSourceCommit') or compiled.get('compiledSourceCommit'),
          'compiledIdentityNote': 'Verified supplied provenance' if compiled else 'Binary hashes recorded; compiled source commit not independently supplied',
          'profileInitiallyEmpty': True, 'restartReusesOnlyThisProfile': True,
          'proxyVariableNamesPresent': [k for k in env if k.lower() in ('http_proxy', 'https_proxy', 'all_proxy', 'no_proxy')]})
    rounds, all_cases = [], []
    active_root = None
    active_profile_id = None
    discovery = {'status': 'NOT_RUN', 'reason': 'SourcesSheet.show automatically prefetches a torrent; requires separate controlled-source/prefetch-policy review before discovery'}
    for phase in ('install', 'restart'):
        phase_out = output / phase
        (phase_out / 'ipc').mkdir(parents=True)
        raw = private / (phase + '.log')
        process = None
        driver = None
        forced, error, code = False, None, None
        with raw.open('w') as log:
            try:
                process = subprocess.Popen([str(binary)], cwd=source, env=env, stdout=log, stderr=log)
                driver = AddonDriver(phase_out, process, tag, profile, raw)
                state = driver.ready(90)
                if not Path(state.get('appDataRoot', '')).is_relative_to(profile / 'data'):
                    raise RuntimeError('App profile isolation failed')
                current_root = Path(state['appDataRoot']).resolve()
                if not current_root.is_relative_to((profile / 'data').resolve()):
                    raise RuntimeError('Resolved app profile isolation failed')
                profile_state = driver.props('stremioSyncState', ['activeProfileId'])
                current_profile_id = profile_state.get('activeProfileId')
                if not valid_profile_id(current_profile_id):
                    raise RuntimeError('Active extension profile identity unavailable or unsafe')
                if active_root is not None and (current_root != active_root or current_profile_id != active_profile_id):
                    raise RuntimeError('Actual active extension root or profile identity changed on persistence restart')
                active_root = current_root
                active_profile_id = current_profile_id
                driver.app_data_root = active_root
                driver.active_profile_id = active_profile_id
                state['extensionProfileIdentity'] = profile_state
                write(phase_out / 'initial-state.json', state)
                driver.open_store()
                all_cases.append(driver.case(phase + '-nyaa', lambda: driver.nyaa(phase == 'install')))
                all_cases.append(driver.case(phase + '-torrentio', lambda: driver.torrentio(phase == 'install')))
            except Exception as exc:
                error = str(exc)
            finally:
                if process is not None:
                    if process.poll() is None and driver:
                        try:
                            driver.call('ui-keypress', {'key': 'Ctrl+Q'})
                            process.wait(timeout=15)
                        except Exception:
                            pass
                    if process.poll() is None:
                        forced = True
                        process.terminate()
                        try:
                            process.wait(timeout=5)
                        except subprocess.TimeoutExpired:
                            process.kill()
                            process.wait(timeout=5)
                    code = process.returncode
        # Only projected owner facts and sanitized diagnostics leave private state.
        try:
            owners = owner_rows(active_root, active_profile_id) if active_root is not None else []
        except RuntimeError as exc:
            owners = []
            error = (error + '; ' if error else '') + 'Owner receipt rejected: ' + str(exc)
        record = {'phase': phase, 'numericExitStatus': code, 'forcedTermination': forced, 'error': error, 'ownerRows': owners, 'activeAppDataRoot': str(active_root) if active_root else None, 'activeExtensionProfileId': active_profile_id}
        rounds.append(record)
        write(phase_out / 'exit.json', record)
        (phase_out / 'app.sanitized.log').write_text(sanitize(raw.read_text(errors='replace')))
    persisted = persistence_pass(rounds[0]['ownerRows'], rounds[1]['ownerRows'])
    passed = persisted and len(all_cases) == 4 and all(c['status'] == 'PASS' for c in all_cases) and all(r['numericExitStatus'] == 0 and not r['forcedTermination'] and not r['error'] for r in rounds)
    summary = {'installationGatePassed': passed, 'persistencePassed': persisted, 'cases': all_cases, 'rounds': rounds,
               'sourceDiscovery': discovery,
               'mediaDownloadOrPlayback': 'NOT_RUN'}
    write(output / 'summary.json', summary)
    print(json.dumps(sanitize(summary)))
    return 0 if passed else 1


if __name__ == '__main__':
    sys.exit(main())
