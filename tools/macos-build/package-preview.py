#!/usr/bin/env python3
"""Stage and audit an ad-hoc signed arm64 preview. Requires Apple's command-line tools.

Application resources deliberately live outside Qt's deployment-owned Resources/qml.
Any unresolved/ambiguous dyld reference or newer deployment minimum aborts packaging.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import plistlib
import posixpath
import re
import shutil
import socket
import subprocess
import tempfile
import time
import stat
import zipfile

MACH_MAGICS = {b'\xfe\xed\xfa\xce', b'\xce\xfa\xed\xfe', b'\xfe\xed\xfa\xcf',
               b'\xcf\xfa\xed\xfe', b'\xca\xfe\xba\xbe', b'\xbe\xba\xfe\xca',
               b'\xca\xfe\xba\xbf', b'\xbf\xba\xfe\xca'}
LOAD_COMMANDS = {'LC_LOAD_DYLIB', 'LC_LOAD_WEAK_DYLIB', 'LC_REEXPORT_DYLIB',
                 'LC_LOAD_UPWARD_DYLIB', 'LC_LAZY_LOAD_DYLIB'}
RUNTIME_FORMULAE = ('mpv', 'ffmpeg', 'libtorrent-rasterbar', 'boost', 'openssl@3')


def run(*args):
    print('+', ' '.join(map(str, args)), flush=True)
    result = subprocess.run(list(map(str, args)), check=False, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    print(result.stdout, end='', flush=True)
    result.check_returncode()
    return result.stdout


def within(path, root):
    return path.resolve().is_relative_to(root.resolve())


def system_path(path):
    return posixpath.normpath(path).startswith(('/System/Library/', '/usr/lib/'))


def sha256_file(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def macho_files(app):
    result = []
    for path in sorted(app.rglob('*')):
        if path.is_file() and not path.is_symlink():
            with path.open('rb') as stream:
                if stream.read(4) in MACH_MAGICS:
                    result.append(path)
    if not result:
        raise RuntimeError('No Mach-O files in bundle')
    return result


def parse_load_commands(text):
    dependencies, rpaths, minimums = [], [], []
    for block in re.split(r'Load command \d+\n', text)[1:]:
        match = re.search(r'^\s*cmd (\S+)', block, re.M)
        if not match:
            continue
        cmd = match[1]
        if cmd in LOAD_COMMANDS:
            dependencies.append(re.search(r'^\s*name (.+) \(offset \d+\)', block, re.M)[1])
        elif cmd == 'LC_RPATH':
            rpaths.append(re.search(r'^\s*path (.+) \(offset \d+\)', block, re.M)[1])
        elif cmd in ('LC_BUILD_VERSION', 'LC_VERSION_MIN_MACOSX'):
            if cmd == 'LC_BUILD_VERSION':
                platform = re.search(r'^\s*platform (\S+)', block, re.M)
                if not platform or platform[1] not in ('1', 'MACOS', 'macos'):
                    raise RuntimeError('Non-macOS Mach-O platform')
            minimums.append(re.search(r'^\s*(?:minos|version) ([\d.]+)', block, re.M)[1])
    return dependencies, rpaths, minimums


def version(value):
    parts = tuple(map(int, value.split('.')))
    return parts + (0,) * (3 - len(parts))


def expand_anchor(value, loader, executable):
    if value == '@loader_path' or value.startswith('@loader_path/'):
        return loader.parent / value[len('@loader_path'):].lstrip('/')
    if value == '@executable_path' or value.startswith('@executable_path/'):
        return executable.parent / value[len('@executable_path'):].lstrip('/')
    if value.startswith('/'):
        return Path(value)
    raise RuntimeError(f'Unsupported dyld anchor {value!r} in {loader}')


def resolve_dependency(name, loader, executable, rpaths, app):
    if system_path(name):
        return None  # Apple shared-cache libraries need not exist on disk.
    if name.startswith('@rpath/'):
        candidates = [base / name[len('@rpath/'):] for base in rpaths]
    else:
        candidates = [expand_anchor(name, loader, executable)]
    for candidate in candidates:
        if not within(candidate, app):
            raise RuntimeError(f'External dependency candidate: {loader}: {name}: {candidate}')
    matches = {candidate.resolve() for candidate in candidates if candidate.is_file()}
    if len(matches) != 1:
        raise RuntimeError(f'Unresolved or ambiguous dependency: {loader}: {name}: {sorted(map(str, matches))}')
    return matches.pop()


def inspect_macho(path):
    run('lipo', path, '-verify_arch', 'arm64')
    return parse_load_commands(run('otool', '-arch', 'arm64', '-l', path))


def executable_context(path, app):
    """The nearest enclosing .app owns a nested helper's executable_path."""
    for parent in path.parents:
        if parent.suffix == '.app':
            with (parent / 'Contents/Info.plist').open('rb') as stream:
                name = plistlib.load(stream)['CFBundleExecutable']
            return (parent / 'Contents/MacOS' / name).resolve()
        if parent == app:
            break
    raise RuntimeError(f'Mach-O has no application executable context: {path}')


def normalize_rpaths(app, files):
    frameworks = app / 'Contents/Frameworks'
    for path in files:
        executable = executable_context(path, app)
        _, rpaths, _ = inspect_macho(path)
        for value in rpaths:
            # Never translate a missing dependency by basename. Only discard search
            # paths outside this bundle; macdeployqt must already have rewritten IDs.
            try:
                internal = within(expand_anchor(value, path, executable), app)
            except RuntimeError:
                raise RuntimeError(f'Unsupported RPATH requiring review: {path}: {value}')
            if not internal:
                run('install_name_tool', '-delete_rpath', value, path)
        anchor = '@loader_path/' + os.path.relpath(frameworks, path.parent)
        if anchor not in rpaths:
            run('install_name_tool', '-add_rpath', anchor, path)


def audit(app, maximum='26.1'):
    app = app.resolve()
    for path in app.rglob('*'):
        if path.is_symlink() and not within(path, app):
            raise RuntimeError(f'External bundle symlink: {path}')
    files = macho_files(app)
    metadata = {path.resolve(): inspect_macho(path) for path in files}
    executables = []
    for path in files:
        _, _, minimums = metadata[path.resolve()]
        if not minimums or any(version(value) > version(maximum) for value in minimums):
            raise RuntimeError(f'Missing or incompatible macOS minimum: {path}: {minimums}')
        if re.search(r'\bEXECUTE\b', run('otool', '-arch', 'arm64', '-hv', path)):
            executables.append(path.resolve())
    main = (app / 'Contents/MacOS/colosseum').resolve()
    if main not in executables:
        raise RuntimeError('Main executable missing from Mach-O inventory')
    # Audit every plugin/dylib, including dynamically loaded files not reachable
    # from a static load graph. Traverse every nested executable in its own dyld
    # context throughout its dependency graph.
    queue = [(path, executable_context(path, app)) for path in metadata]
    queue += [(path, path) for path in executables]
    seen, edges = set(), []
    while queue:
        path, executable = queue.pop()
        # Every image received its own Frameworks anchor before this audit. Use
        # that image's paths plus this executable's paths, independently of load
        # order; do not accept closure that relies on an accidental caller stack.
        key = (path, executable)
        if key in seen:
            continue
        seen.add(key)
        if path not in metadata:
            raise RuntimeError(f'Non-Mach-O dependency: {path}')
        dependencies, raw_rpaths, _ = metadata[path]
        own = tuple(expand_anchor(value, path, executable).resolve() for value in raw_rpaths)
        if any(not within(base, app) for base in own):
            raise RuntimeError(f'External RPATH: {path}: {own}')
        executable_paths = tuple(expand_anchor(value, executable, executable).resolve()
                                 for value in metadata[executable][1])
        if any(not within(base, app) for base in executable_paths):
            raise RuntimeError(f'External executable RPATH: {executable}')
        rpaths = tuple(dict.fromkeys(own + executable_paths))
        for name in dependencies:
            target = resolve_dependency(name, path, executable, rpaths, app)
            edges.append(dict(loader=str(path.relative_to(app)), dependency=name,
                              executable=str(executable.relative_to(app)),
                              resolved=str(target.relative_to(app)) if target else 'Apple system'))
            if target:
                queue.append((target, executable))
    return dict(status='PASS', architecture='arm64', maximum_macos_minimum=maximum,
                macho_count=len(files), executable_count=len(executables), edges=edges)


def stage_runtime(source, app):
    runtime = app / 'Contents/Resources/colosseum-runtime'
    runtime.mkdir(parents=True, exist_ok=False)
    for directory in ('qml', 'assets', 'resources'):
        shutil.copytree(source / directory, runtime / directory, symlinks=False)
    (runtime / 'data').mkdir()
    shutil.copy2(source / 'data/title-identities-v1.json', runtime / 'data/title-identities-v1.json')
    macos = app / 'Contents/MacOS'
    shutil.move(str(macos / 'qml-build.manifest'), runtime / 'qml-build.manifest')
    shutil.move(str(macos / 'scripts'), runtime / 'scripts')
    # macdeployqt attempts to sign every regular file in MacOS. Reject unexpected
    # non-executables rather than silently ship a build output or local state.
    for path in macos.iterdir():
        if not path.is_file() or path.name != 'colosseum':
            raise RuntimeError(f'Unexpected pre-deployment MacOS entry: {path}')
    return runtime


def copy_qt_license_directories(qt, notices):
    seen = set()
    for name in ('Licenses', 'LICENSES', 'licenses'):
        directory = qt / name
        if directory.is_dir():
            stat = directory.stat()
            identity = (stat.st_dev, stat.st_ino)
            if identity in seen:
                continue
            seen.add(identity)
            # Numbered destinations also avoid case collisions if distinct
            # source directories came from a case-sensitive volume.
            shutil.copytree(directory, notices / ('Qt-licenses-' + str(len(seen))))


def runtime_formula_closure(formulae, roots=RUNTIME_FORMULAE):
    by_name = {}
    for formula in formulae:
        if not formula.get('installed'):
            continue
        for name in (formula['name'], formula.get('full_name', formula['name']), *formula.get('aliases', [])):
            by_name[name] = formula
    selected, pending = {}, list(roots)
    while pending:
        name = pending.pop()
        if name not in by_name:
            raise RuntimeError(f'Runtime dependency absent from installed Homebrew metadata: {name}')
        formula = by_name[name]
        if formula['name'] in selected:
            continue
        selected[formula['name']] = formula
        pending.extend(formula.get('dependencies', []))
    return [selected[name] for name in sorted(selected)]


def startup_ready(state, splash):
    return (any(window.get('visible') and window.get('width', 0) > 0
                and window.get('height', 0) > 0 for window in state.get('windows', []))
            and splash.get('props', {}).get('visible') is False)


def verify_webengine_entitlements(app, report_path):
    """Check preserved SDK helper metadata, without claiming functional rendering."""
    report = dict(status='FAIL', scope='QtWebEngine helper signing metadata only', helpers=[])
    try:
        helpers, seen = [], set()
        for candidate in sorted(app.rglob('QtWebEngineProcess.app')):
            helper = candidate.resolve(strict=True)
            if not helper.is_dir() or not within(helper, app):
                raise RuntimeError(f'Invalid QtWebEngine helper bundle: {candidate}')
            stat = helper.stat()
            identity = (stat.st_dev, stat.st_ino)
            if identity not in seen:
                seen.add(identity)
                helpers.append(helper)
        if not helpers:
            raise RuntimeError('No QtWebEngineProcess.app helper found')
        for helper in helpers:
            record = dict(helper=str(helper.relative_to(app.resolve())), status='FAIL')
            report['helpers'].append(record)
            try:
                expected_path = helper / 'Contents/Resources/QtWebEngineProcess.entitlements'
                if not within(expected_path, app):
                    raise RuntimeError('QtWebEngine SDK entitlement file escapes bundle')
                expected = plistlib.loads(expected_path.read_bytes())
                if not isinstance(expected, dict) or not expected:
                    raise RuntimeError('QtWebEngine SDK entitlement plist must be a nonempty dictionary')
                record['expected'] = expected
                # Apple TN3125 documents --xml because the default display is
                # a human-readable DER representation on current macOS.
                command = ['codesign', '--display', '--entitlements', '-', '--xml', str(helper)]
                print('+', ' '.join(command), flush=True)
                # codesign diagnostics belong to stderr; merging them with the
                # plist would corrupt the actual entitlement evidence.
                result = subprocess.run(command, check=False, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                print(result.stderr.decode('utf-8', errors='replace'), end='', flush=True)
                result.check_returncode()
                actual = plistlib.loads(result.stdout, fmt=plistlib.FMT_XML)
                if not isinstance(actual, dict) or not actual:
                    raise RuntimeError('Signed QtWebEngine entitlements must be a nonempty dictionary')
                record['actual'] = actual
                mismatches = [key for key, value in expected.items()
                              if key not in actual or type(actual[key]) is not type(value) or actual[key] != value]
                if mismatches:
                    raise RuntimeError('SDK QtWebEngine entitlements missing or changed: ' + ', '.join(mismatches))
                record['status'] = 'PASS'
            except Exception as error:
                record['error'] = str(error)
                raise
        report['status'] = 'PASS'
        return report
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        report_path.write_text(json.dumps(report, indent=2))


def package(receipt_path, output):
    receipt = json.loads(receipt_path.read_text())
    source, original = Path(receipt['source']), Path(receipt['bundle'])
    if not receipt['bundle'] or not original.is_dir():
        raise RuntimeError('Receipt does not describe a built .app')
    actual_sha = run('git', '-C', source, 'rev-parse', 'HEAD').strip()
    if actual_sha != receipt['source_sha']:
        raise RuntimeError('Source HEAD changed after compilation')
    if run('git', '-C', source, 'status', '--porcelain', '--untracked-files=normal').strip():
        raise RuntimeError('Source checkout is no longer clean')
    app = output / 'Colosseum.app'
    if app.exists():
        raise RuntimeError('Refusing to overwrite staged application')
    run('ditto', original, app)
    runtime = stage_runtime(source, app)
    info_path = app / 'Contents/Info.plist'
    with info_path.open('rb') as stream:
        info = plistlib.load(stream)
    info.update(CFBundleName='Colosseum', CFBundleDisplayName='Colosseum',
                CFBundleIdentifier='world.getcolosseum.Colosseum', CFBundleExecutable='colosseum',
                CFBundleVersion='1.2.0', CFBundleShortVersionString='1.2.0', LSMinimumSystemVersion='26.1')
    with info_path.open('wb') as stream:
        plistlib.dump(info, stream)
    notices = app / 'Contents/Resources/licenses'
    notices.mkdir()
    for name in ('LICENSE', 'THIRD_PARTY_NOTICES.md'):
        shutil.copy2(source / name, notices / name)
    qt = Path(receipt['qt_prefix'])
    for license_path in qt.iterdir():
        if license_path.is_file() and re.match(r'^(?:LICENSE|LICENCE|COPYING|COPYRIGHT|NOTICE)(?:[._-]|$)', license_path.name, re.I):
            destination = notices / 'Qt-top-level' / license_path.name
            destination.parent.mkdir(exist_ok=True)
            shutil.copy2(license_path, destination)
    copy_qt_license_directories(qt, notices)
    # Preserve available Homebrew license/copyright files without copying caches
    # or a second dependency runtime. Include transitive dependency provenance.
    native_provenance = json.loads(run('brew', 'info', '--json=v2', '--installed'))
    formulae = runtime_formula_closure(native_provenance['formulae'])
    active_prefixes = {}
    for formula in formulae:
        # Resolve Homebrew's active opt symlink, never enumerate old Cellar kegs.
        prefix = Path(run('brew', '--prefix', formula['name']).strip()).resolve(strict=True)
        active_prefixes[formula['name']] = str(prefix)
        for license_path in prefix.rglob('*'):
            if license_path.is_file() and re.fullmatch(r'(?:LICENSE|LICENCE|COPYING|COPYRIGHT|NOTICE)(?:[._-].*)?', license_path.name, re.I):
                destination = notices / 'Homebrew' / formula['name'] / license_path.relative_to(prefix)
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(license_path, destination)
    (notices / 'homebrew-runtime.json').write_text(json.dumps(
        dict(roots=RUNTIME_FORMULAE, formulae=formulae, active_prefixes=active_prefixes), indent=2))
    for name in ('ecm', 'mpvqt'):
        source_dir = Path(receipt['work']) / (name + '-source')
        if (source_dir / 'LICENSES').is_dir():
            shutil.copytree(source_dir / 'LICENSES', notices / name)
        for license_path in source_dir.iterdir():
            if license_path.is_file() and re.match(r'^(?:LICENSE|LICENCE|COPYING|COPYRIGHT|NOTICE)(?:[._-]|$)', license_path.name, re.I):
                destination = notices / (name + '-top-level') / license_path.name
                destination.parent.mkdir(exist_ok=True)
                shutil.copy2(license_path, destination)
    (notices / 'build-receipt.json').write_text(json.dumps(receipt, indent=2))
    for name in ('ffmpeg', 'ffprobe'):
        binary = Path(run('brew', '--prefix', 'ffmpeg').strip()) / 'bin' / name
        run('lipo', binary, '-verify_arch', 'arm64')
        shutil.copy2(binary, app / 'Contents/MacOS' / name)
    deploy = qt / 'bin/macdeployqt'
    run(deploy, app, '-qmldir=' + str(source / 'qml'),
        '-executable=' + str(app / 'Contents/MacOS/ffmpeg'),
        '-executable=' + str(app / 'Contents/MacOS/ffprobe'), '-codesign=-', '-verbose=2')
    normalize_rpaths(app, macho_files(app))
    report = audit(app)
    (output / 'dependency-audit.json').write_text(json.dumps(report, indent=2))
    identity = dict(version='1.2.0', source_sha=actual_sha, architecture='arm64',
                    minimum_macos='26.1', signing='ad-hoc', developer_id=False, notarized=False,
                    runtime_status='UNVERIFIED', receipt=receipt,
                    qml_manifest_sha256=hashlib.sha256((runtime / 'qml-build.manifest').read_bytes()).hexdigest())
    (app / 'Contents/Resources/BUILD-IDENTITY.json').write_text(json.dumps(identity, indent=2))
    run('codesign', '--force', '--deep', '--sign', '-',
        '--preserve-metadata=identifier,entitlements', app)
    run('codesign', '--verify', '--deep', '--strict', '--verbose=2', app)
    verify_webengine_entitlements(app, output / 'signing-entitlements.json')
    run('codesign', '--display', '--verbose=4', app)
    archive = output / 'Colosseum-1.2.0-macOS-arm64-preview.zip'
    if archive.exists():
        raise RuntimeError('Refusing to overwrite archive')
    partial = archive.with_suffix('.zip.partial')
    run('ditto', '-c', '-k', '--sequesterRsrc', '--keepParent', app, partial)
    archive_audit = verify_archive(partial)
    (output / 'archive-audit.json').write_text(json.dumps(archive_audit, indent=2))
    partial.rename(archive)
    digest = sha256_file(archive)
    (output / (archive.name + '.sha256')).write_text(digest + '  ' + archive.name + '\n')
    (output / 'BUILD-IDENTITY.json').write_text(json.dumps(identity, indent=2))


def verify_archive(archive):
    """Verify bytes and extraction paths without weakening Qt framework symlinks."""
    try:
        with zipfile.ZipFile(archive) as zipped:
            members = zipped.infolist()
            seen, links, paths = set(), set(), []
            for item in members:
                name = item.filename.rstrip('/')
                parts = name.split('/')
                if (not name or item.orig_filename != item.filename or '\\' in name
                        or name.startswith('/') or any(part in ('', '.', '..') for part in parts)
                        or parts[0] not in ('Colosseum.app', '__MACOSX')):
                    raise RuntimeError(f'Unsafe archive member: {item.filename!r}')
                key = name.casefold()
                if key in seen:
                    raise RuntimeError(f'Archive path collision: {name}')
                seen.add(key)
                paths.append(name)
                kind = stat.S_IFMT(item.external_attr >> 16)
                if kind not in (0, stat.S_IFREG, stat.S_IFDIR, stat.S_IFLNK):
                    raise RuntimeError(f'Unsupported archive member type: {name}')
                if kind == stat.S_IFLNK:
                    target = zipped.read(item).decode('utf-8')
                    resolved = posixpath.normpath(posixpath.join(posixpath.dirname(name), target))
                    if (not target or target.startswith('/') or '\\' in target or '\x00' in target
                            or parts[0] != 'Colosseum.app'
                            or not (resolved == 'Colosseum.app' or resolved.startswith('Colosseum.app/'))):
                        raise RuntimeError(f'Unsafe archive symlink: {name}: {target!r}')
                    links.add(key)
            if not any(path.startswith('Colosseum.app/') for path in paths):
                raise RuntimeError('Archive has no application contents')
            for name in paths:
                components = name.casefold().split('/')
                if any('/'.join(components[:i]) in links for i in range(1, len(components))):
                    raise RuntimeError(f'Archive member descends through symlink: {name}')
            bad = zipped.testzip()
            if bad:
                raise RuntimeError(f'Archive CRC failure: {bad}')
            return dict(status='PASS', members=len(members), symlinks=len(links), crc='PASS')
    except (zipfile.BadZipFile, UnicodeError) as error:
        raise RuntimeError(f'Invalid preview archive: {error}') from error


def isolated_roots(state, tag):
    expected = 'Colosseum-dltest-' + tag
    return all(isinstance(state.get(key), str) and Path(state[key]).is_absolute()
               and Path(state[key]).name == expected for key in ('appDataRoot', 'cacheRoot'))


def smoke(output):
    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith(('QT', 'QML', 'COLOSSEUM_', 'DYLD_', 'MACOSX_'))}
    report = dict(archive='NOT RUN', version='NOT RUN', gui='NOT RUN', normal_quit='NOT RUN',
                  profile_isolation='NOT RUN', physical_m1_playback_gpu_audio='NOT RUN')
    try:
        with tempfile.TemporaryDirectory(prefix='colosseum-smoke-') as directory:
            cwd = Path(directory)
            # Exercise the actual archive from a fresh path outside source/build.
            archive = output / 'Colosseum-1.2.0-macOS-arm64-preview.zip'
            report['archive'] = 'FAIL'
            report['archive_audit'] = verify_archive(archive)
            report['archive'] = 'PASS'
            run('ditto', '-x', '-k', archive, cwd)
            app = cwd / 'Colosseum.app'
            run('codesign', '--verify', '--deep', '--strict', '--verbose=2', app)
            executable = app / 'Contents/MacOS/colosseum'
            report['version'] = 'FAIL'
            result = subprocess.run([str(executable), '--version'], cwd=cwd, env=environment,
                                    capture_output=True, text=True, timeout=30)
            if result.returncode or result.stdout.strip() != 'Colosseum 1.2.0':
                raise RuntimeError(f'--version failed: {result}')
            report['version'] = 'PASS'
            # A console user alone does not prove GUI operation, but no console
            # session is an explicit infrastructure block rather than app failure.
            console = subprocess.check_output(['stat', '-f', '%Su', '/dev/console'], text=True).strip()
            if console in ('root', 'loginwindow', ''):
                report['gui'] = report['normal_quit'] = 'BLOCKED: no logged-in GUI console session'
            else:
                report['gui'] = 'FAIL'
                address = cwd / 'lanista.sock'
                tag = 'mac-preview-' + str(os.getpid())
                environment.update(COLOSSEUM_LANISTA_PIPE=str(address), COLOSSEUM_LANISTA_DRIVE='1',
                                   COLOSSEUM_APPDATA_TAG=tag)
                with (output / 'runtime-startup.log').open('w') as log:
                    process = subprocess.Popen([str(executable)], cwd=cwd, env=environment, stdout=log, stderr=log)
                    try:
                        deadline = time.monotonic() + 120
                        reply, splash = {}, {}
                        while time.monotonic() < deadline and process.poll() is None:
                            try:
                                reply = lanista(address, 'get-state')
                                splash = lanista(address, 'qml-get', {'object': 'bootSplash', 'props': ['visible']})
                                if startup_ready(reply, splash):
                                    break
                            except (OSError, ValueError, RuntimeError):
                                pass
                            time.sleep(1)
                        (output / 'runtime-get-state.json').write_text(json.dumps(reply, indent=2))
                        (output / 'runtime-boot-splash.json').write_text(json.dumps(splash, indent=2))
                        if not startup_ready(reply, splash):
                            raise RuntimeError('Normal launch did not report a visible window and completed boot splash within 120 seconds')
                        report['app_data_root'] = reply.get('appDataRoot')
                        report['cache_root'] = reply.get('cacheRoot')
                        report['profile_isolation'] = 'FAIL'
                        if not isolated_roots(reply, tag):
                            raise RuntimeError('Resolved application/cache roots do not match isolated profile tag')
                        report['profile_isolation'] = 'PASS'
                        report['gui'] = 'PASS: visible window and completed boot splash reported by Lanista only'
                        report['normal_quit'] = 'FAIL'
                        lanista(address, 'ui-keypress', {'key': 'Ctrl+Q'})
                        if process.wait(timeout=45) != 0:
                            raise RuntimeError('Application normal quit returned nonzero')
                        report['normal_quit'] = 'PASS'
                    finally:
                        if process.poll() is None:
                            process.terminate()
                            try:
                                process.wait(timeout=10)
                            except subprocess.TimeoutExpired:
                                process.kill()
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        (output / 'runtime-smoke.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2))


def lanista(address, command, payload=None):
    with socket.socket(socket.AF_UNIX) as client:
        client.settimeout(5)
        client.connect(str(address))
        client.sendall(json.dumps(dict(seq=1, cmd=command, payload=payload or {})).encode() + b'\n')
        data = b''
        while b'\n' not in data:
            block = client.recv(65536)
            if not block or len(data) > 4 * 1024 * 1024:
                raise RuntimeError('Missing or oversized Lanista reply')
            data += block
        reply = json.loads(data.split(b'\n')[0])
        if reply.get('type') != 'reply':
            raise RuntimeError(str(reply))
        return reply


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=('package', 'smoke'))
    parser.add_argument('--receipt', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    try:
        if args.mode == 'package':
            if args.receipt is None:
                parser.error('--receipt is required for package')
            package(args.receipt, args.output)
        else:
            smoke(args.output)
    except Exception as error:
        (args.output / (args.mode + '-failure.json')).write_text(json.dumps(dict(status='FAIL', error=str(error)), indent=2))
        raise


if __name__ == '__main__':
    main()
