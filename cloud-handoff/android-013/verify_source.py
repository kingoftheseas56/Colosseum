"""Verify exact imported source. Python stdlib only; does not mutate files."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()

def verify(manifest, app, donor, labels=None):
    errors, checked = [], 0
    for label, root in [('app', app), ('donor', donor)]:
        if labels and label not in labels:
            continue
        for entry in manifest['repositories'][label]['files']:
            rel = entry['path']
            target = (root / rel).resolve()
            if not target.is_relative_to(root.resolve()):
                raise ValueError('Unsafe manifest path')
            if not target.is_file():
                errors.append({'repo': label, 'path': rel, 'reason': 'missing'})
            elif target.stat().st_size != entry['bytes'] or digest(target) != entry['sha256']:
                errors.append({'repo': label, 'path': rel, 'reason': 'byte mismatch'})
            checked += 1
    return {'checked': checked, 'mismatches': errors, 'status': 'PASS' if not errors else 'FAIL'}

if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--manifest', type=Path, default=Path(__file__).parent / 'CLOUD-SOURCE-MANIFEST.json')
    p.add_argument('--app', required=True, type=Path)
    p.add_argument('--donor', required=True, type=Path)
    args = p.parse_args()
    result = verify(json.loads(args.manifest.read_text(encoding='utf-8')), args.app, args.donor)
    print(json.dumps(result, indent=2))
    sys.exit(0 if result['status'] == 'PASS' else 1)
