#!/usr/bin/env python3
"""Apply only the reviewed patch series and verify exact base-plus-patch content."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

BASE = '36498faa7cbcf1c6f47bdea042806b1aa8135f7e'
HERE = Path(__file__).resolve().parent
PATCHES = [
    HERE / 'patches/0001-reader2-profile-runtime-link.patch',
    HERE / 'patches/0002-account-first-light-delivery-link.patch',
    HERE / 'patches/0003-linux-clang-tidy-reviewed-findings.patch',
    HERE / 'patches/0004-portable-account-test-fixtures.patch',
    HERE / 'patches/0005-linux-preview-credential-errors.patch',
    HERE / 'patches/0006-keyboard-test-layout-readiness.patch',
    HERE / 'patches/0007-datastore-test-socket-lifetime.patch',
    HERE / 'patches/0008-portable-runtime-credential-fixtures.patch',
    HERE / 'patches/0009-player-activity-tracker-scope.patch',
]


def git(source, *args, **kwargs):
    return subprocess.check_output(['git', '-C', str(source), *args], **kwargs)


def base_check(source):
    if git(source, 'rev-parse', 'HEAD').decode().strip() != BASE:
        raise RuntimeError('source base SHA mismatch')


def expected_tree(source):
    with tempfile.TemporaryDirectory() as directory:
        env = dict(os.environ, GIT_INDEX_FILE=str(Path(directory) / 'index'))
        git(source, 'read-tree', BASE, env=env)
        for patch in PATCHES:
            git(source, 'apply', '--cached', '--whitespace=error', str(patch), env=env)
        return git(source, 'write-tree', env=env).decode().strip()


def verify(source):
    base_check(source)
    tree = expected_tree(source)
    if git(source, 'ls-files', '--others', '--exclude-standard').strip():
        raise RuntimeError('untracked files in application checkout')
    if git(source, 'diff', '--binary', tree, '--'):
        raise RuntimeError('application checkout differs from exact base plus patches')
    diff = git(source, 'diff', '--binary', BASE, '--')
    return {'source_base_sha': BASE, 'source_tree_sha': tree,
            'controller_sha': git(HERE, 'rev-parse', 'HEAD').decode().strip(),
            'source_patches': [{'name': p.name, 'sha256': hashlib.sha256(p.read_bytes()).hexdigest()} for p in PATCHES],
            'source_diff_sha256': hashlib.sha256(diff).hexdigest()}


def apply(source):
    base_check(source)
    if git(source, 'status', '--porcelain', '--untracked-files=all').strip():
        raise RuntimeError('source must be pristine before applying patches')
    expected_tree(source)  # Validate the entire series before changing the worktree.
    for patch in PATCHES:
        git(source, 'apply', '--whitespace=error', str(patch))
    return verify(source)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--evidence', type=Path, required=True)
    args = parser.parse_args()
    info = apply(args.source)
    args.evidence.mkdir(parents=True, exist_ok=True)
    (args.evidence / 'source-provenance.json').write_text(json.dumps(info, indent=2) + '\n')
    (args.evidence / 'source-applied.diff').write_bytes(git(args.source, 'diff', '--binary', BASE, '--'))
    (args.evidence / 'source-patches.sha256').write_text(''.join(p['sha256'] + '  ' + p['name'] + '\n' for p in info['source_patches']))


if __name__ == '__main__':
    main()
