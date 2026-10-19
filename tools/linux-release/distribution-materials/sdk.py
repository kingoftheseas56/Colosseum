#!/usr/bin/env python3
"""Verify shipped SDK ELF bytes against official Qt installer archives."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import tempfile
import xml.etree.ElementTree as ET
from distro import Commands
from inventory import digest, safe_name, write_json
from upstream import download, retain_notices

REPOSITORY = 'https://download.qt.io/online/qtsdkrepository/linux_x64/desktop/qt6_6111/qt6_6111/'
WEBENGINE_REPOSITORY = 'https://download.qt.io/online/qtsdkrepository/linux_x64/extensions/qtwebengine/6111/x86_64/'


def collect(out, webengine_only=False):
    root = out / 'sdk-verification'
    root.mkdir(parents=True, exist_ok=True)
    commands = Commands(root)
    metadata = root / 'Updates.xml'
    download(REPOSITORY + 'Updates.xml', metadata)
    packages = ET.parse(metadata).findall('PackageUpdate')
    extension_metadata = root / 'WebEngine-Updates.xml'
    download(WEBENGINE_REPOSITORY + 'Updates.xml', extension_metadata)
    extension_packages = ET.parse(extension_metadata).findall('PackageUpdate')
    extension_names = {p.findtext('Name') for p in extension_packages}
    packages += extension_packages
    build = json.loads((out / 'retained/payload/opt/colosseum/BUILD-IDENTITY.json').read_text())
    inventory = json.loads((out / 'payload-inventory.json').read_text())
    origins = build['bundled_dependency_origins']
    targets = {}
    for item in inventory:
        if not item.get('elf') or not item['path'].startswith('opt/colosseum/usr/'):
            continue
        path = item['path']
        original = origins.get(Path(path).name, {})
        relative = path.removeprefix('opt/colosseum/usr/')
        if '/Qt/6.11.1/gcc_64/' in original.get('origin', ''):
            relative = original['origin'].split('/Qt/6.11.1/gcc_64/', 1)[1]
        elif not relative.startswith(('qml/', 'plugins/', 'libexec/')):
            continue
        if path == 'opt/colosseum/usr/plugins/imageformats/libqtiff.so':
            continue  # Rebuilt from source, not the SDK copy.
        targets[relative] = {'payload': path, 'shipped_sha256': item['sha256'], **original}
    modules = ['qtimageformats', 'qtpositioning', 'qtserialport', 'qtwebchannel', 'qtwebsockets']
    names = ['qt.qt6.6111.linux_gcc_64'] + ['qt.qt6.6111.addons.' + m + '.linux_gcc_64' for m in modules]
    names += ['extensions.qtwebengine.6111.linux_gcc_64']
    proofs = json.loads((out / 'qt-sdk-file-proofs.json').read_text()) if webengine_only else []
    receipts = json.loads((out / 'qt-sdk-archive-index.json').read_text()) if webengine_only else []
    if webengine_only:
        names = ['extensions.qtwebengine.6111.linux_gcc_64']
    errors = []
    for name in names:
        package = next((p for p in packages if p.findtext('Name') == name), None)
        if package is None:
            errors.append({'package': name, 'reason': 'Installer package absent from current Qt metadata'})
            continue
        version = package.findtext('Version')
        for filename in package.findtext('DownloadableArchives', '').split(', '):
            record = {'package': name, 'version': version, 'archive': filename, 'status': 'unresolved'}
            archive = root / 'temporary-archives' / filename
            try:
                if safe_name(filename) != Path(filename).name or '/' in version:
                    raise ValueError('Invalid installer metadata path')
                repository = WEBENGINE_REPOSITORY if name in extension_names else REPOSITORY
                url = repository + name + '/' + version + filename
                checksum_file = root / 'archive-checksums' / (filename + '.sha1')
                download(url + '.sha1', checksum_file)
                checksum = checksum_file.read_text().strip().split()[0]
                if not re.fullmatch('[0-9a-fA-F]{40}', checksum):
                    raise ValueError('No official installer archive SHA1')
                resolved = download(url, archive)
                with archive.open('rb') as stream:
                    actual = hashlib.file_digest(stream, 'sha1').hexdigest()
                if actual != checksum.lower():
                    raise ValueError('Official installer archive SHA1 mismatch')
                record.update(url=url, resolved_url=resolved, official_sha1=actual,
                              observed_sha256=digest(archive), bytes=archive.stat().st_size)
                listing = commands.run(['7z', 'l', '-slt', str(archive)])
                selected = {}
                for line in listing.splitlines():
                    if not line.startswith('Path = '):
                        continue
                    member = line[7:]
                    if member == str(archive):
                        continue
                    member = safe_name(member)
                    # SDK archives have either an SDK version prefix or a lib
                    # payload (ICU); operations in Updates.xml specify the latter.
                    relative = member.removeprefix('6.11.1/gcc_64/')
                    if filename.startswith('icu-'):
                        relative = 'lib/' + relative.removeprefix('lib/')
                    if relative in targets:
                        selected[member] = relative
                with tempfile.TemporaryDirectory(prefix='sdk-proof-') as temporary:
                    tmp = Path(temporary)
                    if selected:
                        commands.run(['7z', 'x', '-y', '-o' + temporary, str(archive), *selected])
                    for member, relative in selected.items():
                        target = targets[relative]
                        file = tmp / member
                        if file.is_symlink() or not file.is_file():
                            continue
                        original = digest(file)
                        if target.get('sha256_before_relocation') and original != target['sha256_before_relocation']:
                            errors.append({**target, 'reason': 'Qt archive bytes differ from retained pre-relocation hash', 'archive': filename})
                            continue
                        relative_parent = Path(target['payload']).parent
                        import os
                        rpath = '$ORIGIN/' + os.path.relpath('opt/colosseum/usr/lib', relative_parent)
                        commands.run(['patchelf', '--set-rpath', rpath, str(file)])
                        transformed = digest(file)
                        if transformed != target['shipped_sha256']:
                            errors.append({**target, 'reason': 'Qt archive replay differs from shipped bytes', 'archive': filename})
                            continue
                        proofs.append({**target, 'status': 'verified-sdk-file', 'archive': filename,
                                       'installer_package': name, 'installer_version': version,
                                       'archive_member': member, 'original_sha256': original,
                                       'archive_sha256': record['observed_sha256']})
                record['status'] = 'verified-archive'
            except Exception as exc:
                record['reason'] = str(exc)
            finally:
                # Only this collector's temporary download; binary SDK archives
                # are not advertised as source material or republished.
                if archive.exists():
                    archive.unlink()
            receipts.append(record)
            write_json(out / 'qt-sdk-file-proofs.json', proofs)
            write_json(out / 'qt-sdk-archive-index.json', receipts)
            write_json(out / 'qt-sdk-unresolved.json', errors)
            print(filename + ': ' + record['status'] + f'; {len(proofs)} total file proofs', flush=True)
    proven = {p['payload'] for p in proofs}
    errors += [{**t, 'reason': 'No exact matching official SDK archive file verified'}
               for t in targets.values() if t['payload'] not in proven]
    write_json(out / 'qt-sdk-unresolved.json', errors)
    if webengine_only:
        return  # The authenticated cache already contains the ICU candidate inputs.

    # Acquire upstream ICU source as a candidate input, without treating a
    # version match as proof of Qt's private prebuilt build/patch configuration.
    candidate = {'component': 'ICU73.2 upstream candidate', 'status': 'unresolved'}
    try:
        source_root = out / 'upstream/icu-candidate'
        source_root.mkdir(parents=True, exist_ok=True)
        base = 'https://github.com/unicode-org/icu/releases/download/release-73-2/'
        sha_file = source_root / 'SHASUM512.txt'
        download(base + 'SHASUM512.txt', sha_file)
        filename = 'icu4c-73_2-src.tgz'
        checksum = next(line.split()[0] for line in sha_file.read_text().splitlines()
                        if line.split() and line.split()[-1].lstrip('*') == filename)
        archive = source_root / filename
        resolved = download(base + filename, archive)
        with archive.open('rb') as stream:
            actual = hashlib.file_digest(stream, 'sha512').hexdigest()
        if actual != checksum:
            raise ValueError('Unicode ICU SHA512 metadata mismatch')
        candidate.update(status='acquired-unproven-sdk-correspondence', url=base + filename,
                         resolved_url=resolved, sha512=actual, observed_sha256=digest(archive),
                         path=archive.relative_to(out).as_posix(),
                         retained_notice_files=retain_notices(archive, source_root / 'notices'))
        url = 'https://raw.githubusercontent.com/qt/qt5/v6.11.1/coin/provisioning/qtci-linux-RHEL-9.6-x86_64/30-install_icu.sh'
        download(url, source_root / 'qt-icu-provisioning.sh')
        candidate['qt_provisioning_script_url'] = url
        candidate['limitation'] = 'Qt script installs a prebuilt ICU73.2 binary; it does not supply its original build recipe or prove this source tarball includes any Qt prebuilt patches.'
    except Exception as exc:
        candidate['reason'] = str(exc)
    write_json(out / 'icu-candidate-source.json', candidate)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--webengine-only', action='store_true')
    args = parser.parse_args()
    collect(args.output.resolve(), args.webengine_only)
