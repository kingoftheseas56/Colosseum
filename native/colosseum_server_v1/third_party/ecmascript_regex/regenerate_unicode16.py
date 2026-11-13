#!/usr/bin/env python3
"""Rebuild the native regex tables from hash-pinned official Unicode 16 data.

Requires Python 3 and a C compiler, not Node or a JavaScript engine. Downloaded
inputs and build products stay in --cache, outside the repository by default.
The upstream generator format is retained; only script names absent from the
selected UCD version are removed from its definitions before generation.
"""
import argparse
import hashlib
import os
from pathlib import Path
import re
import subprocess
import tarfile
import tempfile
import urllib.request
import zipfile

SOURCES = {
    'quickjs-2026-06-04.tar.xz': (
        'https://bellard.org/quickjs/quickjs-2026-06-04.tar.xz',
        'b376e839b322978313d929fd20663b11ba58b75df5a46c126dd19ea2fa70ad2a'),
    'UCD-16.0.0.zip': (
        'https://www.unicode.org/Public/zipped/16.0.0/UCD.zip',
        'c86dd81f2b14a43b0cc064aa5f89aa7241386801e35c59c7984e579832634eb2'),
    'emoji-sequences.txt': (
        'https://www.unicode.org/Public/emoji/16.0/emoji-sequences.txt',
        '3fe3c77e72e8f26df302dc7d99b106c5d08fd808ef7246fb5d4502d659fe659c'),
    'emoji-zwj-sequences.txt': (
        'https://www.unicode.org/Public/emoji/16.0/emoji-zwj-sequences.txt',
        '9423ec235474356f970a696506737e2d5d65453a67f45df66b8bbe920c3fab83'),
}


def rebuild(cache, output, compiler):
    cache.mkdir(parents=True, exist_ok=True)
    for name, (url, digest) in SOURCES.items():
        target = cache / name
        if not target.exists():
            with urllib.request.urlopen(url, timeout=60) as response:
                target.write_bytes(response.read())
        if hashlib.sha256(target.read_bytes()).hexdigest() != digest:
            raise RuntimeError(f'Checksum mismatch: {target}')

    quickjs = cache / 'quickjs'
    unicode = cache / 'unicode16'
    quickjs.mkdir(exist_ok=True)
    unicode.mkdir(exist_ok=True)
    # Extract only the generator and its dependencies, never an archive's paths.
    with tarfile.open(cache / 'quickjs-2026-06-04.tar.xz') as archive:
        for name in ['unicode_gen.c', 'unicode_gen_def.h', 'cutils.c', 'cutils.h']:
            data = archive.extractfile('quickjs-2026-06-04/' + name).read()
            (quickjs / name).write_bytes(data)
    with zipfile.ZipFile(cache / 'UCD-16.0.0.zip') as archive:
        for name in ['UnicodeData.txt', 'SpecialCasing.txt', 'CaseFolding.txt',
                     'CompositionExclusions.txt', 'DerivedCoreProperties.txt',
                     'DerivedNormalizationProps.txt', 'PropList.txt', 'Scripts.txt',
                     'ScriptExtensions.txt', 'PropertyValueAliases.txt',
                     'emoji/emoji-data.txt']:
            (unicode / Path(name).name).write_bytes(archive.read(name))
    for name in ['emoji-sequences.txt', 'emoji-zwj-sequences.txt']:
        (unicode / name).write_bytes((cache / name).read_bytes())

    aliases = set()
    for line in (unicode / 'PropertyValueAliases.txt').read_text().splitlines():
        fields = [value.strip() for value in line.split('#')[0].split(';')]
        if fields[0] == 'sc':
            aliases.update(fields[1:])
    definitions = quickjs / 'unicode_gen_def.h'
    original = definitions.read_text()
    start = original.index('#ifdef UNICODE_SCRIPT')
    end = original.index('#endif', start)
    scripts = re.sub(r'^DEF\((\w+),[^\n]+\n',
                     lambda match: match[0] if match[1] in aliases else '',
                     original[start:end], flags=re.MULTILINE)
    definitions.write_text(original[:start] + scripts + original[end:])

    generator = cache / 'unicode_gen'
    subprocess.run([compiler, '-O2', str(quickjs / 'unicode_gen.c'),
                    str(quickjs / 'cutils.c'), '-o', str(generator)], check=True)
    subprocess.run([str(generator), str(unicode), str(output.resolve())], check=True)
    print(hashlib.sha256(output.read_bytes()).hexdigest(), output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cache', type=Path)
    parser.add_argument('--output', type=Path,
                        default=Path(__file__).with_name('libunicode-table.h'))
    parser.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    args = parser.parse_args()
    if args.cache:
        rebuild(args.cache.resolve(), args.output, args.cc)
    else:
        with tempfile.TemporaryDirectory(prefix='server1-unicode16-') as directory:
            rebuild(Path(directory), args.output, args.cc)


if __name__ == '__main__':
    main()
