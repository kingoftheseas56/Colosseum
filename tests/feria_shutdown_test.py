"""Run the Feria UI journey and require a clean exit, not a harness timeout kill."""
import json
import os
from pathlib import Path
import re
import subprocess
import sys


def main():
    root = Path(__file__).resolve().parents[1]
    scenario = (sys.argv[1] if len(sys.argv) > 1
                else 'tests/lanista_scenarios/feria_account_native.json')
    env = os.environ.copy()
    env.pop('COLOSSEUM_DEV', None)
    result = subprocess.run(
        [str(root / 'native/build-msvc/lanista.exe'), 'session', 'run', scenario,
         '--drive', '--ready-ms', '60000', '--timings'],
        cwd=root, env=env, capture_output=True, text=True, timeout=180)
    print(result.stdout, end='')
    print(result.stderr, end='', file=sys.stderr)
    match = re.search(r'\(manifest: (.+?/session\.json)\)', result.stdout)
    if result.returncode or not match:
        return 1
    manifest = json.loads((root / match[1]).read_text(encoding='utf-8'))
    log = (root / manifest['stderrPath']).read_text(encoding='utf-8', errors='replace')
    clean = (manifest['killReason'] == 'graceful'
             and manifest['exitCode'] == 0 and not manifest['crashed']
             and 'QSqlDatabase requires a QCoreApplication' not in log)
    print('SHUTDOWN', 'PASS' if clean else 'FAIL',
          manifest['killReason'], 'exitCode=' + str(manifest['exitCode']))
    return 0 if clean else 1


if __name__ == '__main__':
    sys.exit(main())
