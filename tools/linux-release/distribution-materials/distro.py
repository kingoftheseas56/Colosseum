#!/usr/bin/env python3
"""Evidence-based Ubuntu ownership and authenticated exact-version acquisition."""
import argparse
import hashlib
import json
import os
import posixpath
from pathlib import Path
import re
import subprocess
import tarfile
import tempfile
from inventory import digest, safe_name, write_json


class Commands:
    def __init__(self, output):
        self.root = output / 'commands'
        self.root.mkdir(parents=True, exist_ok=True)
        self.sequence = max((int(p.stem) for p in self.root.glob('*.txt') if p.stem.isdigit()), default=0)

    def run(self, argv, cwd=None, check=True):
        self.sequence += 1
        result = subprocess.run(argv, cwd=cwd, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT)
        log = self.root / f'{self.sequence:04d}.txt'
        log.write_text(result.stdout, encoding='utf-8')
        with (self.root / 'receipts.jsonl').open('a', encoding='utf-8') as stream:
            stream.write(json.dumps({'argv': argv, 'cwd': str(cwd or Path.cwd()),
                                     'exit': result.returncode, 'log': log.name}) + '\n')
        if check and result.returncode:
            raise RuntimeError(f'{argv!r}: exit {result.returncode}; see {log.name}')
        return result.stdout


def paragraphs(text):
    for raw in re.split(r'\n\s*\n', text.strip()):
        row = {}
        key = None
        for line in raw.splitlines():
            if line.startswith((' ', '\t')) and key:
                row[key] += '\n' + line.strip()
            elif ': ' in line:
                key, value = line.split(': ', 1)
                row[key] = value
            elif line.endswith(':'):
                key = line[:-1]
                row[key] = ''
        if row:
            yield raw, row


def source_identity(binary):
    # Debian control Source defaults are defined by the package format; never
    # derive a source name/version from a SONAME or library naming convention.
    match = re.fullmatch(r'([^ ]+)(?: \(([^)]+)\))?', binary.get('Source', binary['Package']))
    if not match:
        raise ValueError('Invalid binary Source field')
    return match[1], match[2] or binary['Version']


def source_acquire(name, version, out, commands):
    directory = out / 'sources' / (name + '_' + version.replace(':', '%3a'))
    directory.mkdir(parents=True, exist_ok=True)
    raw = commands.run(['apt-cache', 'showsrc', '--only-source', name])
    found = [(text, row) for text, row in paragraphs(raw)
             if row.get('Package') == name and row.get('Version') == version]
    if not found:
        raise ValueError(f'Exact authenticated Sources stanza unavailable: {name}={version}')
    text, stanza = found[0]
    (directory / 'Sources-stanza.txt').write_text(text + '\n', encoding='utf-8')
    # APT suppresses URIs for files already present in cwd. Query in a fresh
    # empty directory so resume preserves a complete authoritative URL index.
    with tempfile.TemporaryDirectory(prefix='uri-query-', dir=directory) as query:
        uris = commands.run(['apt-get', '--print-uris', 'source', '--download-only',
                             '--only-source', name + '=' + version], cwd=Path(query))
    (directory / 'acquisition-uris.txt').write_text(uris, encoding='utf-8')
    commands.run(['apt-get', '-o', 'Acquire::Retries=3', '-o', 'Acquire::https::Timeout=30',
                  'source', '--download-only', '--only-source',
                  name + '=' + version], cwd=directory)
    files = []
    for line in stanza.get('Checksums-Sha256', '').splitlines():
        if not line.strip():
            continue
        checksum, size, filename = line.split()
        if safe_name(filename) != Path(filename).name:
            raise ValueError('Unsafe source filename')
        file = directory / filename
        actual = digest(file)
        if actual != checksum or file.stat().st_size != int(size):
            raise ValueError('Source checksum/size mismatch: ' + filename)
        # apt supplies exact authoritative URLs; do not invent pool URLs.
        uri = next((line.split("'")[1] for line in uris.splitlines()
                    if line.startswith("'") and line.split()[1] == filename), None)
        if not uri:
            raise ValueError('Missing authenticated acquisition URI for ' + filename)
        files.append({'path': file.relative_to(out).as_posix(), 'sha256': actual,
                      'bytes': int(size), 'url': uri})
    if not files or not any(f['path'].endswith('.dsc') for f in files):
        raise ValueError('Incomplete source file set')
    return {'source_package': name, 'source_version': version, 'status': 'verified-acquired',
            'metadata': (directory / 'Sources-stanza.txt').relative_to(out).as_posix(),
            'files': files, 'trust': 'APT authenticated Ubuntu Sources metadata; hashes compared locally'}


def binary_files(archive, requested, notice_dir, retained_host=None):
    """Read exact file payloads and notices from a binary archive, not SONAME aliases."""
    found = {}
    notice_links = []
    process = subprocess.Popen(['dpkg-deb', '--fsys-tarfile', str(archive)], stdout=subprocess.PIPE)
    with tarfile.open(fileobj=process.stdout, mode='r|') as tar:
        for member in tar:
            path = '/' + safe_name(member.name)
            if member.issym() and path.startswith('/usr/share/doc/'):
                resolved = posixpath.normpath(posixpath.join(posixpath.dirname(path), member.linkname))
                record = {'archive_path': path, 'symlink_target': member.linkname,
                          'resolved_path': resolved, 'status': 'unresolved'}
                if not resolved.endswith('/copyright'):
                    resolved += '/copyright'
                if retained_host and resolved.startswith('/usr/share/doc/'):
                    original = retained_host / resolved.lstrip('/')
                    if original.is_file():
                        target = notice_dir / 'retained-original-host' / resolved.lstrip('/')
                        target.parent.mkdir(parents=True, exist_ok=True)
                        target.write_bytes(original.read_bytes())
                        record.update(status='retained-original-host-text', sha256=digest(target),
                                      retained_original_path=str(original), retained_copy=str(target))
                notice_links.append(record)
            if not member.isfile():
                continue
            wanted = path in requested
            notice = path.startswith('/usr/share/doc/') and path.endswith('/copyright')
            if not (wanted or notice):
                continue
            data = tar.extractfile(member).read()
            if wanted:
                found[path] = data
            if notice:
                target = notice_dir / path.lstrip('/')
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)
    if process.wait() != 0:
        raise RuntimeError('dpkg-deb archive read failed')
    write_json(notice_dir.parent / 'notice-links.json', notice_links)
    return found


def collect(out):
    commands = Commands(out)
    retained = out / 'retained/payload/opt/colosseum'
    build = json.loads((retained / 'BUILD-IDENTITY.json').read_text())
    payload = {r['path']: r for r in json.loads((out / 'payload-inventory.json').read_text())}
    provenance = {}
    for line in (retained / 'licenses/dependency-0-notices/ubuntu-package-provenance.tsv').read_text().splitlines():
        fields = line.split('\t')
        if len(fields) == 4:
            provenance[fields[0]] = fields[1:]
    targets = []
    for soname, origin in build['bundled_dependency_origins'].items():
        if origin['origin'].startswith(('/usr/', '/lib/')):
            targets.append({'payload': 'opt/colosseum/usr/lib/' + soname, **origin})
    # These two copied executables have no pre-relocation hash in BUILD-IDENTITY.
    # Prove them by applying the documented RPATH edit to exact archive bytes.
    for binary in ('ffmpeg', 'ffprobe'):
        targets.append({'payload': 'opt/colosseum/usr/bin/' + binary, 'origin': '/usr/bin/' + binary})
    owners = {}
    proofs = []
    gaps = []
    for target in targets:
        try:
            raw = commands.run(['dpkg-query', '--search', target['origin']])
            candidates = {line.rsplit(': ', 1)[0] for line in raw.splitlines()
                          if ': ' in line and line.rsplit(': ', 1)[1] == target['origin']}
            if len(candidates) != 1:
                raise ValueError('No unique exact-path dpkg owner')
            owner = candidates.pop()
            name = owner.split(':')[0]
            if name not in provenance:
                raise ValueError('Owner absent from original build package provenance')
            target['binary_package'] = name
            target['binary_version'] = provenance[name][0]
            target['ownership_observation'] = raw.strip()
            owners.setdefault((name, target['binary_version']), []).append(target)
        except Exception as exc:
            gaps.append({**target, 'reason': str(exc)})
    sources = {}
    for (name, version), members in sorted(owners.items()):
        directory = out / 'binary-verification' / name
        directory.mkdir(parents=True, exist_ok=True)
        try:
            raw = commands.run(['apt-cache', 'show', name + ':amd64=' + version])
            found = [(text, row) for text, row in paragraphs(raw)
                     if row.get('Package') == name and row.get('Version') == version
                     and row.get('Architecture') == 'amd64' and row.get('SHA256')]
            if not found:
                raise ValueError('Exact binary version absent from authenticated APT Packages metadata')
            text, stanza = found[0]
            (directory / 'Packages-stanza.txt').write_text(text + '\n', encoding='utf-8')
            commands.run(['apt-get', 'download', name + ':amd64=' + version], cwd=directory)
            archives = list(directory.glob('*.deb'))
            if len(archives) != 1:
                raise ValueError('Expected one binary archive')
            archive = archives[0]
            if digest(archive) != stanza['SHA256'] or archive.stat().st_size != int(stanza['Size']):
                raise ValueError('Binary archive hash/size mismatch')
            source_name, source_version = source_identity(stanza)
            if provenance[name][1:] != [source_name, source_version]:
                raise ValueError('Source identity disagrees with retained build provenance')
            data = binary_files(archive, {m['origin'] for m in members}, directory / 'copyrights',
                                retained / 'licenses/dependency-0-notices/host-copyrights')
            notice_files = [{'path': p.relative_to(out).as_posix(), 'sha256': digest(p)}
                            for p in sorted((directory / 'copyrights').rglob('*')) if p.is_file()]
            for member in members:
                try:
                    original = data[member['origin']]
                    before = hashlib.sha256(original).hexdigest()
                    expected = member.get('sha256_before_relocation')
                    if expected and before != expected:
                        raise ValueError('Original file hash differs from BUILD-IDENTITY pre-relocation hash')
                    test_file = directory / 'relocation-check'
                    test_file.write_bytes(original)
                    rpath = '$ORIGIN/.' if '/usr/lib/' in member['payload'] else '$ORIGIN/../lib'
                    commands.run(['patchelf', '--set-rpath', rpath, str(test_file)])
                    transformed = digest(test_file)
                    test_file.unlink()
                    if transformed != payload[member['payload']]['sha256']:
                        raise ValueError('Replayed RPATH transform does not match shipped file')
                    proofs.append({**member, 'status': 'verified', 'archive_sha256': stanza['SHA256'],
                                   'archive_file_path': member['origin'], 'original_sha256': before,
                                   'shipped_sha256': transformed, 'source_package': source_name,
                                   'source_version': source_version,
                                   'binary_copyright_texts': notice_files,
                                   'metadata': (directory / 'Packages-stanza.txt').relative_to(out).as_posix()})
                except Exception as exc:
                    gaps.append({**member, 'reason': str(exc)})
            archive.unlink()  # This task's temporary binary copy only; sources are retained.
            key = (source_name, source_version)
            if key not in sources:
                try:
                    sources[key] = source_acquire(*key, out, commands)
                except Exception as exc:
                    sources[key] = {'source_package': key[0], 'source_version': key[1],
                                    'status': 'unresolved', 'reason': str(exc)}
        except Exception as exc:
            gaps.extend({**member, 'reason': str(exc)} for member in members)
        write_json(out / 'ubuntu-file-proofs.json', proofs)
        write_json(out / 'ubuntu-unresolved.json', gaps)
        write_json(out / 'ubuntu-source-index.json', list(sources.values()))
        print(f'{name}={version}: {len(proofs)} file proofs, {len(gaps)} unresolved', flush=True)
    write_json(out / 'ubuntu-summary.json', {'targets': len(targets), 'verified': len(proofs),
               'unresolved_files': len(gaps), 'source_sets': len(sources),
               'unresolved_sources': sum(s['status'] != 'verified-acquired' for s in sources.values())})
    write_json(out / 'ubuntu-notice-unresolved.json',
               [{'package': name, 'reason': 'Exact binary archive has no regular copyright text; inspect retained host notices, documentation symlinks and matching source debian/copyright before notice acceptance.'}
                for name in sorted({p['binary_package'] for p in proofs if not p['binary_copyright_texts']})])


def resume_sources(out):
    commands = Commands(out)
    index = json.loads((out / 'ubuntu-source-index.json').read_text())
    for i, row in enumerate(index):
        if row['status'] == 'verified-acquired':
            continue
        try:
            index[i] = source_acquire(row['source_package'], row['source_version'], out, commands)
        except Exception as exc:
            row['reason'] = str(exc)
        write_json(out / 'ubuntu-source-index.json', index)
    summary = json.loads((out / 'ubuntu-summary.json').read_text())
    summary['unresolved_sources'] = sum(r['status'] != 'verified-acquired' for r in index)
    write_json(out / 'ubuntu-summary.json', summary)
    print(json.dumps(summary))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--resume-sources', action='store_true')
    args = parser.parse_args()
    (resume_sources if args.resume_sources else collect)(args.output.resolve())
