#!/usr/bin/env python3
"""Stage an auditable Linux tar package; creation is NOT qualification."""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile

from source_patches import verify as verify_source
from qualify import HOST_ABI, is_elf, loader_errors, required_tools

SOURCE = '36498faa7cbcf1c6f47bdea042806b1aa8135f7e'


def run(*args, **kwargs):
    return subprocess.check_output(args, text=True, **kwargs).strip()


def copy_library(source, destination):
    source, destination = Path(source), Path(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.exists():
        if hashlib.sha256(source.read_bytes()).digest() != hashlib.sha256(destination.read_bytes()).digest():
            raise RuntimeError('conflicting library basename: ' + str(destination))
    else:
        shutil.copy2(source, destination, follow_symlinks=True)


def copy_qt_runtime(source, destination):
    # The SDK QML tree also contains build objects and static link metadata.
    # Keep QML/import metadata and shared plugins, but omit development artifacts.
    shutil.copytree(source, destination, symlinks=False,
                    ignore=shutil.ignore_patterns('*.o', '*.obj', '*.a', '*.prl', '*.la'))


def stage_preview_notice(stage):
    """Ship the preview scope from the controller, not the pinned source archive."""
    shutil.copy2(Path(__file__).resolve().parent / 'PREVIEW.md', stage / 'PREVIEW.md')
    return {'release_channel': 'linux-preview',
            'display_name': 'Colosseum 1.1.7 Linux PREVIEW',
            'derived_from_version': '1.1.7', 'preview_notice': 'PREVIEW.md',
            'unsupported_features': ['account credential persistence',
                                     'tracker credential persistence']}


def stage_qt_plugins(qt, stage):
    for required in ['imageformats/libqwebp.so', 'platforms/libqxcb.so', 'sqldrivers/libqsqlite.so']:
        if not (qt / 'plugins' / required).is_file():
            raise RuntimeError('required Qt runtime plugin missing: ' + required)
    for group in ['platforms', 'imageformats', 'iconengines', 'tls', 'xcbglintegrations']:
        copy_qt_runtime(qt / 'plugins' / group, stage / 'usr/plugins' / group)
    # The Qt SDK TIFF plugin links libtiff.so.5; Ubuntu 24.04 supplies .6.
    # TIFF decoding is outside this preview rather than importing an older ABI.
    (stage / 'usr/plugins/imageformats/libqtiff.so').unlink(missing_ok=True)
    (stage / 'usr/plugins/sqldrivers').mkdir(parents=True)
    shutil.copy2(qt / 'plugins/sqldrivers/libqsqlite.so', stage / 'usr/plugins/sqldrivers/libqsqlite.so')
    for plugin in (stage / 'usr/plugins/platforms').iterdir():
        if plugin.name not in ['libqxcb.so', 'libqoffscreen.so']:
            plugin.unlink()


def write_launcher(stage):
    (stage / 'AppRun').write_text('''#!/bin/sh
set -eu
APPDIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-xcb}"
export LD_LIBRARY_PATH="$APPDIR/usr/lib"
export QT_PLUGIN_PATH="$APPDIR/usr/plugins"
export QML2_IMPORT_PATH="$APPDIR/usr/qml"
export QML_IMPORT_PATH="$APPDIR/usr/qml"
export QTWEBENGINEPROCESS_PATH="$APPDIR/usr/libexec/QtWebEngineProcess"
export QTWEBENGINE_RESOURCES_PATH="$APPDIR/usr/resources"
export QTWEBENGINE_LOCALES_PATH="$APPDIR/usr/translations/qtwebengine_locales"
export PATH="$APPDIR/usr/bin:$PATH"
cd "$APPDIR"
exec "$APPDIR/usr/bin/colosseum" "$@"
''')
    (stage / 'AppRun').chmod(0o755)


def main():
    parser = argparse.ArgumentParser()
    for name in ['source', 'build', 'qt', 'mpvqt', 'out']:
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    source, build, qt, mpvqt, out = [getattr(args, n).resolve() for n in ['source', 'build', 'qt', 'mpvqt', 'out']]
    runtime_tools = required_tools()
    if run('git', '-C', str(source), 'rev-parse', 'HEAD') != SOURCE:
        raise RuntimeError('source SHA mismatch')
    source_provenance = verify_source(source)
    cache = (build / 'CMakeCache.txt').read_text()
    for key, value in [('CMAKE_BUILD_TYPE', 'Release'), ('COLOSSEUM_BUILD_PLAYER2', 'OFF'),
                       ('COLOSSEUM_PLAYER2_IN_APP', 'OFF'), ('COLOSSEUM_UPDATE_TESTING', 'OFF')]:
        if not re.search(r'^' + key + r':\w+=' + value + '$', cache, re.M):
            raise RuntimeError('unexpected build cache: ' + key)
    if run(str(qt / 'bin/qmake'), '-query', 'QT_VERSION') != '6.11.1':
        raise RuntimeError('Qt version mismatch')
    out.mkdir(parents=True, exist_ok=True)
    stage = out / 'Colosseum-1.1.7-linux-x86_64'
    stage.mkdir()  # refuse to merge stale staging output
    # Preserve every tracked resource and its relative layout. Deliberately include
    # the exact patched source instead of guessing which JS/scripts/assets are unused.
    archive = subprocess.check_output(['git', '-C', str(source), 'archive', source_provenance['source_tree_sha']])
    with tarfile.open(fileobj=io.BytesIO(archive)) as stream:
        stream.extractall(stage, filter='data')
    lib = stage / 'usr/lib'
    binary = stage / 'usr/bin'
    binary.mkdir(parents=True)
    lib.mkdir(parents=True)
    for name in ['colosseum', 'qml-build.manifest']:
        shutil.copy2(build / name, binary / name)
    # bsdtar provides the libarchive ZIP/CBZ/RAR semantics used by both production
    # archive consumers. These executables enter the same transitive ELF closure.
    for name, path in runtime_tools.items():
        shutil.copy2(path, binary / name)
    for name in ['qml', 'resources', 'translations', 'libexec']:
        copy_qt_runtime(qt / name, stage / 'usr' / name)
    # SQLite is the app's SQL backend. Do not ship unused SQL drivers whose
    # vendor clients (Oracle/MySQL/PostgreSQL) would enlarge the runtime contract.
    # Optional platform plugins bring unrelated Wayland/minimal/VNC dependencies.
    stage_qt_plugins(qt, stage)
    for prefix in [qt / 'lib', mpvqt / 'lib', mpvqt / 'lib64']:
        if prefix.is_dir():
            for item in prefix.glob('*.so*'):
                if item.is_file():
                    copy_library(item, lib / item.name)
    qtconf = '[Paths]\nPrefix=..\nLibraries=lib\nPlugins=plugins\nQmlImports=qml\nData=.\nTranslations=translations\nLibraryExecutables=libexec\n'
    (binary / 'qt.conf').write_text(qtconf)
    (stage / 'usr/libexec/qt.conf').write_text(qtconf)
    env = os.environ.copy()
    env['LD_LIBRARY_PATH'] = ':'.join(map(str, [lib, qt / 'lib', mpvqt / 'lib', mpvqt / 'lib64']))
    seen = set()
    provenance = {}
    # Resolve transitive ELF dependencies, including dlopen-loaded Qt plugins.
    # libsecret/OpenSSL/xcb-cursor are runtime-loaded and explicitly included.
    output = run('ldconfig', '-p')
    for soname in ['libsecret-1.so.0', 'libssl.so.3', 'libcrypto.so.3', 'libxcb-cursor.so.0']:
        match = re.search(r'^\s*' + re.escape(soname) + r'\s+.*=>\s+(/\S+)', output, re.M)
        if not match:
            raise RuntimeError('runtime-loaded dependency missing: ' + soname)
        copy_library(match[1], lib / soname)
        provenance[soname] = match[1]
    while True:
        pending = [p for p in stage.rglob('*') if p not in seen and is_elf(p)]
        if not pending:
            break
        for path in pending:
            seen.add(path)
            result = subprocess.run(['ldd', str(path)], env=env, text=True, capture_output=True)
            text = result.stdout + result.stderr
            if 'not found' in text or result.returncode:
                raise RuntimeError('unresolved ELF: ' + str(path) + '\n' + text)
            for name, location in re.findall(r'^\s*(\S+)\s+=>\s+(/\S+)', text, re.M):
                if not HOST_ABI.fullmatch(name):
                    copy_library(location, lib / name)
                    provenance.setdefault(name, location)
    for path in seen:
        relative = os.path.relpath(lib, path.parent)
        subprocess.run(['patchelf', '--set-rpath', '$ORIGIN/' + relative, str(path)], check=True)
    # Recheck after rewriting search paths: no builder paths may resolve.
    for path in seen:
        errors = loader_errors(run('ldd', str(path), env=env), stage)
        if errors:
            raise RuntimeError('\n'.join(errors))
    subprocess.run(['cmake', '-DQML_ROOT=' + str(stage / 'qml'), '-DOUTPUT_FILE=' + str(out / 'staged-qml.manifest'),
                    '-P', str(source / 'native/bootstrap/write_qml_build_manifest.cmake')], check=True)
    if (out / 'staged-qml.manifest').read_bytes() != (binary / 'qml-build.manifest').read_bytes():
        raise RuntimeError('binary/QML fingerprint mismatch')
    write_launcher(stage)
    # Preserve license texts and exact installed dependency versions. This package
    # is a review candidate; redistribution license/source obligations need review.
    shutil.copytree('/usr/share/doc', stage / 'usr/share/doc', symlinks=True)
    if (qt / 'Licenses').exists():
        shutil.copytree(qt / 'Licenses', stage / 'usr/share/qt-licenses')
    for name, root in [('mpvqt', mpvqt), ('qt', qt)]:
        (stage / (name + '-prefix.txt')).write_text(str(root) + '\n')
    (stage / 'DEPENDENCY-VERSIONS.txt').write_text(run('dpkg-query', '-W') + '\n')
    (stage / 'PACKAGE.json').write_text(json.dumps({**source_provenance, **stage_preview_notice(stage), 'source_sha': SOURCE, 'version': '1.1.7', 'qt': '6.11.1',
        'ecm': '6.15.0', 'mpvqt': '1.2.0', 'format': 'tar.gz', 'target': 'Ubuntu 24.04 x86_64',
        'created': True, 'qualified': False, 'released': False,
        'host_requirements': ['glibc 2.39 / Ubuntu 24.04', 'X11 display', 'Mesa/OpenGL drivers', 'fonts', 'CA certificates'],
        'limitations': ['Account, tracker and Stremio credential persistence unsupported; no plaintext fallback',
                        'X11 only; no Wayland package qualification', 'No standalone mpv/DVR',
                        'No bundled Stremio service; its routes unqualified', 'No hardware/audio/DRM qualification',
                        'WebEngine resources bundled; reader runtime unverified; TIFF decoding unavailable',
                        'Catalog coverage is live movie production requests, not visual UI/offline data/all providers'],
        'library_origins': provenance}, indent=2) + '\n')
    target = out / (stage.name + '-candidate.tar.gz')
    subprocess.run(['tar', '-czf', str(target), '-C', str(out), stage.name], check=True)
    with target.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    (out / (target.name + '.sha256')).write_text(digest + '  ' + target.name + '\n')
    print(target)


if __name__ == '__main__':
    main()
