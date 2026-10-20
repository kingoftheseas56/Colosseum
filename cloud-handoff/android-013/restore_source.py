"""Restore source recovery ZIP into a fresh directory, verify bytes/modes."""
import argparse
import json
from pathlib import Path
import os
import zipfile
from verify_source import digest, verify

EXPECTED = '920d3e72f4e6d641def87d683ed63e4ae9d82f0bfe66f9190cfe9274543205bf'

if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('destination', type=Path)
    p.add_argument('--archive', type=Path, default=Path(__file__).parent / 'ANDROID-SOURCE-BASELINE.zip')
    args = p.parse_args()
    if digest(args.archive) != EXPECTED:
        raise SystemExit('Source archive hash mismatch')
    if args.destination.exists():
        raise SystemExit('Use a fresh, nonexistent restoration destination')
    dest = args.destination.resolve()
    with zipfile.ZipFile(args.archive) as z:
        seen = set()
        for info in z.infolist():
            path = (dest / info.filename).resolve()
            if not path.is_relative_to(dest) or info.filename in seen or (info.external_attr >> 16) & 0o170000 == 0o120000:
                raise SystemExit('Unsafe/duplicate archive member')
            seen.add(info.filename)
        z.extractall(dest)
    manifest = json.loads((dest / 'SOURCE-MANIFEST.json').read_text(encoding='utf-8'))
    result = verify(manifest, dest / 'source/app', dest / 'source/donor')
    # ZIP on Windows does not restore POSIX executable bits; recover tracked modes.
    if os.name == 'posix':
        for label in ('app', 'donor'):
            for line in (dest / 'provenance' / label / 'git-modes.txt').read_text().splitlines():
                meta, rel = line.split('\t', 1)
                target = dest / 'source' / label / rel
                if target.is_file() and meta.startswith('100755 '):
                    target.chmod(0o755)
    (dest / 'RESTORE-RESULT.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result, indent=2))
    raise SystemExit(0 if result['status'] == 'PASS' else 1)
