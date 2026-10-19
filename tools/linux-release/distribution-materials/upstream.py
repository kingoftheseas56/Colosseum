#!/usr/bin/env python3
"""Acquire upstream inputs with distinct archive, commit and SBOM evidence."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import tarfile
from urllib.request import urlopen
from distro import Commands
from inventory import digest, safe_name, write_json


def download(url, target):
    target.parent.mkdir(parents=True, exist_ok=True)
    with urlopen(url, timeout=180) as response, target.open('wb') as sink:
        final = response.url
        while block := response.read(1024 * 1024):
            sink.write(block)
    return final


def acquire(url, directory, checksum=None):
    filename = url.rsplit('/', 1)[1]
    archive = directory / filename
    directory.mkdir(parents=True, exist_ok=True)
    metadata = None
    if checksum is None:
        metadata = directory / (filename + '.sha256')
        download(url + '.sha256', metadata)
        text = metadata.read_text()
        match = re.search(r'\b([0-9a-fA-F]{64})\b', text)
        if not match:
            raise ValueError('No official SHA256 metadata: ' + url)
        checksum = match[1].lower()
    final = download(url, archive)
    actual = digest(archive)
    if actual != checksum:
        raise ValueError('Upstream checksum mismatch: ' + url)
    return archive, {'url': url, 'resolved_url': final, 'sha256': actual,
                     'bytes': archive.stat().st_size,
                     'checksum_authority': url + '.sha256' if metadata else 'accepted build workflow pin'}


def retain_notices(archive, directory):
    records = []
    with tarfile.open(archive, 'r|*') as tar:
        for member in tar:
            path = safe_name(member.name)
            parts = Path(path).parts[1:]
            if not member.isfile() or not parts:
                continue
            relative = '/'.join(parts)
            base = parts[-1].lower()
            if (any(p.lower() in ('licenses', 'license') for p in parts)
                    or any(word in base for word in ('license', 'copyright', 'notice', 'attribution'))
                    or relative in ('.git_archival.txt', '.cmake.conf', '.gitmodules')):
                # Avoid materializing the Qt/Chromium tree. Retain actual texts.
                target = directory / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                data = tar.extractfile(member).read()
                target.write_bytes(data)
                records.append({'path': relative, 'sha256': hashlib.sha256(data).hexdigest()})
    write_json(directory / 'retained-index.json', records)
    return len(records)


def git_source(module, ref, root, commands):
    git = root / 'git' / module
    git.mkdir(parents=True, exist_ok=True)
    commands.run(['git', 'init', str(git)])
    repository = 'qtimageformats' if module == 'qtimageformats-tiff' else module
    url = 'https://github.com/qt/' + repository + '.git'
    commands.run(['git', '-C', str(git), 'remote', 'add', 'origin', url])
    commands.run(['git', '-C', str(git), 'fetch', '--depth=1', 'origin', ref])
    commit = commands.run(['git', '-C', str(git), 'rev-parse', 'FETCH_HEAD']).strip()
    if re.fullmatch('[0-9a-f]{40}', ref) and commit != ref:
        raise ValueError('Wrong upstream source commit')
    archive = root / (module + '-' + commit + '.tar.gz')
    commands.run(['git', '-C', str(git), 'archive', '--format=tar.gz',
                  '--prefix=' + module + '-' + commit + '/', '--output=' + str(archive), commit])
    commit_text = commands.run(['git', '-C', str(git), 'cat-file', '-p', commit])
    (root / (module + '-commit.txt')).write_text(commit_text, encoding='utf-8')
    tree_text = commands.run(['git', '-C', str(git), 'ls-tree', '-r', commit])
    (root / (module + '-git-tree.txt')).write_text(tree_text, encoding='utf-8')
    files = {}
    submodules = []
    for line in tree_text.splitlines():
        meta, path = line.split('\t', 1)
        mode, kind, checksum = meta.split()
        if kind == 'blob':
            files[path] = checksum
        elif kind == 'commit':
            submodules.append({'path': path, 'commit': checksum})
    return {'repository': url, 'requested_ref': ref, 'commit': commit,
            'archive': archive.name, 'archive_sha256': digest(archive),
            'submodules': submodules}, files


def compare_archive(archive, expected):
    matched = set()
    different = []
    with tarfile.open(archive, 'r|*') as tar:
        for member in tar:
            path = '/'.join(safe_name(member.name).split('/')[1:])
            if path not in expected or not (member.isfile() or member.issym()):
                continue
            if member.issym():
                data = member.linkname.encode()
                actual = hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()
            else:
                hasher = hashlib.sha1(b'blob ' + str(member.size).encode() + b'\0')
                stream = tar.extractfile(member)
                while block := stream.read(1024 * 1024):
                    hasher.update(block)
                actual = hasher.hexdigest()
            if actual == expected[path]:
                matched.add(path)
            else:
                different.append(path)
    return {'matched_git_blobs': len(matched), 'different_git_blobs': different,
            'absent_git_paths': sorted(set(expected) - matched - set(different)),
            'scope': 'top-level module tracked files only; gitlinks require separate source evidence'}


def collect(out):
    root = out / 'upstream'
    root.mkdir(parents=True, exist_ok=True)
    commands = Commands(root)
    sboms = out / 'retained/payload/opt/colosseum/licenses/Qt-SBOM'
    index = []
    for sbom in sorted(sboms.glob('*.spdx.json')):
        module = sbom.name.split('-6.11.1')[0]
        if '-chromium-' in module:
            continue
        document = json.loads(sbom.read_text())
        package = next((p for p in document.get('packages', []) if p.get('name') == module), None)
        if not package:
            continue
        record = {'component': module, 'version': '6.11.1', 'sbom': str(sbom.relative_to(out)),
                  'sbom_source_location': package.get('downloadLocation'),
                  'recorded_build_configuration': package.get('comment'), 'status': 'unresolved'}
        try:
            loc = package.get('downloadLocation', '')
            match = re.search(r'@([0-9a-f]{40})$', loc)
            ref = match[1] if match else 'refs/tags/v6.11.1'
            vcs, files = git_source(module, ref, root, commands)
            record['git_source'] = vcs
            url = ('https://download.qt.io/archive/qt/6.11/6.11.1/submodules/'
                   + module + '-everywhere-src-6.11.1.tar.xz')
            archive, acquired = acquire(url, root / module)
            acquired['path'] = str(archive.relative_to(out))
            record['archive'] = acquired
            record['archive_vs_commit'] = compare_archive(archive, files)
            record['retained_notice_files'] = retain_notices(archive, root / module / 'notices')
            # Source input acquired; this does not certify the SDK binary was
            # reproduced, or that every transitive gitlink has been accounted for.
            record['status'] = 'acquired-review-required'
        except Exception as exc:
            record['reason'] = str(exc)
        index.append(record)
        write_json(out / 'upstream-source-index.json', index)
        print(module + ': ' + record['status'], flush=True)
    for name, url, checksum in [
        ('mpvqt', 'https://download.kde.org/stable/mpvqt/mpvqt-1.2.0.tar.xz',
         '8660ad79c0d60fed77f29b36e1742841466af5405de702c81a121e6eeb625ebb'),
        ('extra-cmake-modules', 'https://download.kde.org/Attic/frameworks/6.15/extra-cmake-modules-6.15.0.tar.xz',
         'f7cd022095a9e6bdbe5897720a24bfda81d211757b7c08b173061962bf2ee0b2')]:
        record = {'component': name, 'status': 'unresolved'}
        try:
            archive, acquired = acquire(url, root / name, checksum)
            acquired['path'] = str(archive.relative_to(out))
            record.update(archive=acquired, retained_notice_files=retain_notices(archive, root / name / 'notices'),
                          status='verified-acquired')
        except Exception as exc:
            record['reason'] = str(exc)
        index.append(record)
        write_json(out / 'upstream-source-index.json', index)
    # Rebuilt plugin source is a separate input from the SDK qtimageformats.
    record = {'component': 'rebuilt-QTiffPlugin', 'status': 'unresolved'}
    try:
        vcs, _ = git_source('qtimageformats-tiff', '77f35e5694885844c3eb3a737769b5ed290b8ccc', root, commands)
        record.update(git_source=vcs, retained_notice_files=retain_notices(root / vcs['archive'], root / 'rebuilt-tiff/notices'), status='verified-acquired')
    except Exception as exc:
        record['reason'] = str(exc)
    index.append(record)
    write_json(out / 'upstream-source-index.json', index)
    # The retained SDK explicitly supplies no source location for its ICU73.2
    # shared libraries. Matching a version string alone is insufficient.
    write_json(out / 'upstream-unresolved.json', [
        {'component': 'Qt SDK ICU 73.2 shared libraries',
         'reason': 'Retained qtbase SBOM downloadLocation=NOASSERTION. Exact SDK ICU build recipe/patch provenance not retained; source equivalence remains unproved.'},
        {'component': 'Qt WebEngine Chromium source closure',
         'reason': 'Inspect archive/gitlink and Chromium SBOM receipts; source archive acquisition alone does not establish all SDK patches/configuration or complete corresponding-source delivery.'}])


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    collect(parser.parse_args().output.resolve())
