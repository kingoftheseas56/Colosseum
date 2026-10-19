#!/usr/bin/env python3
"""Verify all retained inputs before reusing prior CI collection receipts."""
import argparse
import json
from pathlib import Path
from inventory import PACKAGE_SHA256, digest, safe_name


def verify(out):
    identity = json.loads((out / 'package-identity.json').read_text())
    if identity['sha256'] != PACKAGE_SHA256:
        raise ValueError('Wrong cached binary identity')
    index = json.loads((out / 'SHA256-INDEX.json').read_text())
    for row in index:
        file = out / safe_name(row['path'])
        if not file.is_file() or file.stat().st_size != row['bytes'] or digest(file) != row['sha256']:
            raise ValueError('Cache missing or hash mismatch: ' + row['path'])
    print(f'Verified {len(index)} cached files against the pinned review/source artifact index')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    verify(parser.parse_args().output.resolve())
