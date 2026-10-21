"""Task014: source/engine/runtime manifest for the diagnostic APK artifact.

Usage: python3 write_manifest.py <out-dir> <package>
Reads the verification records written into <out-dir> by the workflow.
"""
import json
import os
import pathlib
import re
import sys

out = pathlib.Path(sys.argv[1])
package = sys.argv[2]
verify = (out / 'final-apk-verify.txt').read_text()


def field(pattern, text=verify):
    m = re.search(pattern, text)
    return m.group(1).strip() if m else None


manifest = {
    'task': 'claude-android-local-014-20261021',
    'source_commit': os.environ.get('GITHUB_SHA'),
    'run_id': os.environ.get('GITHUB_RUN_ID'),
    'run_attempt': os.environ.get('GITHUB_RUN_ATTEMPT'),
    'package': package,
    'abi': 'arm64-v8a',
    'apk': 'colosseum-arm64-v8a-debug.apk',
    'apk_bytes': field(r'apk_bytes=(\d+)'),
    'apk_sha256': field(r'apk_sha256=([0-9a-f]{64})'),
    'signer_dn': field(r'Signer #1 certificate DN: (.+)'),
    'signer_sha256': field(r'Signer #1 certificate SHA-256 digest: ([0-9a-f]+)'),
    'engine_member_sha256': field(r'engine_member_sha256=([0-9a-f]{64})'),
    'engine_prefix_sha256': field(r'engine_prefix_sha256=([0-9a-f]{64})'),
    'runtime_bundle_sha256': field(r'bundleSha256=([0-9a-f]{64})'),
    'zipalign_16k': 'zipalign_16k=OK' in verify,
    'engine_stamp': (out / 'engine-stamp.txt').read_text(errors='replace'),
    'configure_args': (out / 'configure-args.txt').read_text().split(),
    'cmake_cache_identity': (out / 'cmake-cache-identity.txt').read_text().splitlines(),
}
missing = [k for k, v in manifest.items() if v in (None, '', False)]
(out / 'MANIFEST.json').write_text(json.dumps(manifest, indent=1) + '\n')
print(json.dumps(manifest, indent=1))
if missing:
    sys.exit('manifest fields missing: ' + ', '.join(missing))
