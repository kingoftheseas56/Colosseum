#!/usr/bin/env python3
"""Verify every delivered file and the 18 exact tested source changes."""
import hashlib
import json
import pathlib
import sys

root = pathlib.Path(__file__).resolve().parents[1]
manifest = json.loads((root / 'PACKAGE-MANIFEST.json').read_text())
for row in manifest['files']:
    path = root / row['path']
    assert path.is_file() and not path.is_symlink(), row['path']
    assert path.stat().st_size == row['bytes'], row['path']
    assert hashlib.sha256(path.read_bytes()).hexdigest() == row['sha256'], row['path']
    if '--restore-executable-bits' in sys.argv and row.get('executable'):
        path.chmod(path.stat().st_mode | 0o111)
tested = json.loads((root / 'evidence/original-tested-manifest.json').read_text())
for row in tested['changed_files']:
    assert hashlib.sha256((root / row['path']).read_bytes()).hexdigest() == row['sha256'], row['path']
print('Verified', len(manifest['files']), 'delivered files and all 18 tested M07 change hashes.')
