#!/usr/bin/env python3
"""Small review archive; large acquired source archives are a separate artifact."""
import argparse
from pathlib import Path
import tarfile


def bundle(output, archive):
    with tarfile.open(archive, 'w:gz') as tar:
        for path in sorted(output.rglob('*')):
            relative = path.relative_to(output)
            if not path.is_file() or 'git' in relative.parts or '.git' in relative.parts:
                continue
            if path.name.endswith(('.tar.xz', '.tar.gz', '.tar.bz2', '.deb', '.diff.gz')):
                continue
            tar.add(path, arcname=relative.as_posix(), recursive=False)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--archive', type=Path, required=True)
    args = parser.parse_args()
    bundle(args.output, args.archive)
