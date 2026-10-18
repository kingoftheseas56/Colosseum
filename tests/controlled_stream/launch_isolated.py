#!/usr/bin/env python3
"""Run the controlled component only inside a verified disposable user/net namespace."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from check_isolation import safe_diagnostic


def main():
    ap = argparse.ArgumentParser()
    for name in ('fixture-dir', 'runtime-dir', 'runtime-provenance', 'output'):
        ap.add_argument('--' + name, type=Path, required=True)
    ap.add_argument("--source-commit", required=True)
    args = ap.parse_args()
    out = args.output.resolve()
    if out.exists():
        raise RuntimeError('Fresh evidence directory required')
    out.mkdir(parents=True)
    result = {'status': 'BLOCKED', 'scope': 'isolated transport component only; no full app', 'fallbackAttempted': False}
    parent_net = os.readlink('/proc/self/ns/net')
    parent_user = os.readlink('/proc/self/ns/user')
    try:
        if sys.platform != 'linux' or os.geteuid() == 0 or os.environ.get('GITHUB_ACTIONS') != 'true':
            raise RuntimeError('Requires nonroot GitHub Linux runner')
        unshare, ip = shutil.which('unshare'), shutil.which('ip')
        if not unshare or not ip:
            raise RuntimeError('Required namespace tools unavailable')
        here = Path(__file__).resolve().parent
        identity = ['--parent-net', parent_net, '--parent-user', parent_user,
                    '--parent-uid', str(os.getuid()), '--parent-gid', str(os.getgid()), '--ip', ip]
        checker = [sys.executable, str(here / 'check_isolation.py'), '--inside', *identity]
        driver = [sys.executable, str(here / 'stream_component.py'), '--source-commit', args.source_commit, '--fixture-dir', str(args.fixture_dir.resolve()),
                  '--runtime-dir', str(args.runtime_dir.resolve()), '--runtime-provenance', str(args.runtime_provenance.resolve()),
                  '--isolation-proof', str(out / 'isolation-proof.json'), *identity, '--output', str(out / 'component')]
        # One shell remains in the newly created namespace for both checker and driver.
        import shlex
        script = shlex.join(checker) + ' > ' + shlex.quote(str(out / 'isolation-proof.json')) + '\nexec ' + shlex.join(driver)
        with tempfile.TemporaryDirectory(prefix='b1-isolation-private-', dir=out.parent) as private:
            log = Path(private) / 'launcher.log'
            with log.open('w') as stream:
                proc = subprocess.Popen([unshare, '--user', '--map-root-user', '--net', 'bash', '-eu', '-c', script],
                                        stdout=stream, stderr=stream, start_new_session=True)
                try:
                    code = proc.wait(timeout=225)
                except subprocess.TimeoutExpired:
                    import signal
                    os.killpg(proc.pid, signal.SIGTERM)
                    try: proc.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        os.killpg(proc.pid, signal.SIGKILL)
                        proc.wait(timeout=5)
                    raise RuntimeError('Isolated component deadline exceeded')
            result['exitCode'] = code
            receipt_path = out / 'component/component-result.json'
            if receipt_path.is_file():
                receipt = json.loads(receipt_path.read_text())
                status = receipt.get('status')
                if status not in ('PASS', 'FAIL', 'BLOCKED') or receipt.get('qualificationSourceCommit') != args.source_commit:
                    raise RuntimeError('Invalid component receipt identity or status')
                if (code == 0) != (status == 'PASS'):
                    raise RuntimeError('Component receipt and process exit disagree')
                result['status'] = status
            elif code == 0:
                raise RuntimeError('Missing component receipt after successful process exit')
            if code:
                result['diagnostic'] = safe_diagnostic(log.read_text(errors='replace')[-2000:])
    except Exception as exc:
        result['diagnostic'] = safe_diagnostic(str(exc))
    result['parentNamespaceUnchanged'] = (os.readlink('/proc/self/ns/net') == parent_net and os.readlink('/proc/self/ns/user') == parent_user)
    if not result['parentNamespaceUnchanged']:
        result['status'] = 'BLOCKED'
    (out / 'launcher-result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))
    return 0 if result['status'] == 'PASS' else 2

if __name__ == '__main__':
    raise SystemExit(main())
