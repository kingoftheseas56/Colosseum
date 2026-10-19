#!/usr/bin/env python3
"""Stream one pinned Debian package; retain evidence, never extract its runtime."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import tarfile

PACKAGE_SHA256 = 'e35e704c901cd67421679fdc82fe4925f6fd28518a1c926c477bcb471df034ad'
PACKAGE_BYTES = 333718172


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def write_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n', encoding='utf-8')


def safe_name(name):
    name = name.removeprefix('./')
    p = PurePosixPath(name)
    if p.is_absolute() or '..' in p.parts or '\\' in name:
        raise ValueError('Unsafe archive path: ' + name)
    return str(p)


def collect(package, output):
    identity = {'file': package.name, 'bytes': package.stat().st_size, 'sha256': digest(package)}
    if identity['sha256'] != PACKAGE_SHA256 or identity['bytes'] != PACKAGE_BYTES:
        raise ValueError('Refusing a package other than the exact accepted candidate')
    output.mkdir(parents=True, exist_ok=True)
    inventory = []
    with package.open('rb') as ar:
        if ar.read(8) != b'!<arch>\n':
            raise ValueError('Not ar')
        while header := ar.read(60):
            if len(header) != 60 or header[58:] != b'`\n':
                raise ValueError('Invalid ar header')
            name = header[:16].decode('ascii').strip().rstrip('/')
            size = int(header[48:58])
            offset = ar.tell()
            if name.startswith(('data.tar', 'control.tar')):
                section = 'payload' if name.startswith('data.') else 'control'
                with tarfile.open(fileobj=ar, mode='r|*') as archive:
                    for member in archive:
                        relative = safe_name(member.name)
                        record = {'path': relative, 'section': section, 'size': member.size,
                                  'kind': 'file' if member.isfile() else 'symlink' if member.issym() else 'other'}
                        if member.issym() or member.islnk():
                            record['target'] = member.linkname
                        if member.isfile():
                            retain = (section == 'control' or relative.startswith('opt/colosseum/licenses/')
                                      or relative in ('opt/colosseum/BUILD-IDENTITY.json', 'opt/colosseum/README-LINUX.txt'))
                            target = output / 'retained' / section / relative
                            if retain:
                                target.parent.mkdir(parents=True, exist_ok=True)
                            stream = archive.extractfile(member)
                            hasher = hashlib.sha256()
                            first = True
                            sink = target.open('wb') if retain else None
                            try:
                                while block := stream.read(1024 * 1024):
                                    if first:
                                        record['elf'] = block.startswith(b'\x7fELF')
                                        first = False
                                    hasher.update(block)
                                    if sink:
                                        sink.write(block)
                            finally:
                                if sink:
                                    sink.close()
                            record['sha256'] = hasher.hexdigest()
                            if retain:
                                record['retained'] = target.relative_to(output).as_posix()
                        inventory.append(record)
            ar.seek(offset + size + size % 2)
    write_json(output / 'package-identity.json', identity)
    write_json(output / 'payload-inventory.json', inventory)
    print(json.dumps({'package': identity, 'entries': len(inventory),
                      'elf_files': sum(bool(r.get('elf')) for r in inventory),
                      'retained_files': sum('retained' in r for r in inventory)}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--package', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    collect(args.package, args.output)
