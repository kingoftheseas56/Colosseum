"""Focused Feria A/B/C checks; use --app to include the assembled Main shell.

Pass --qt-root when Qt's bin/qml folders are not already on the environment.
All browser profiles and Main AppData identities are disposable. No logins.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import uuid


SMOKES = {
    'FeriaNavigationSmoke': 16,
    'FeriaContextualBackSmoke': 12,
    'FeriaSessionRetentionSmoke': 10,
    'FeriaDirectionalNavigationSmoke': 125,
    'FeriaRestorationInputSmoke': 5,
    'FeriaRestorationEdgesSmoke': 34,
    'FeriaChromeSmoke': 50,
    'FeriaSeeAllSmoke': 37,
}


def run_check(command, environment, minimum, timeout, label, output_dir):
    try:
        result = subprocess.run(command, env=environment, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=timeout)
        code, raw = result.returncode, result.stdout
    except subprocess.TimeoutExpired as error:
        code, raw = 'timeout', error.stdout or b''
    checks = []
    for line in raw.decode('utf-8', errors='replace').splitlines():
        if 'FERIA_MAIN_SMOKE ' in line:
            line = line.split('FERIA_MAIN_SMOKE ', 1)[1]
        try:
            value = json.loads(line)
            if isinstance(value, dict) and 'success' in value:
                checks.append(value)
        except ValueError:
            pass
    failures = [value for value in checks if value['success'] is not True]
    ok = code == 0 and len(checks) >= minimum and not failures
    print(json.dumps({'test': label, 'exit': code, 'checks': len(checks),
                      'minimum': minimum, 'failures': failures, 'success': ok}), flush=True)
    if output_dir:
        (output_dir / (label + '.log')).write_bytes(raw)
    elif not ok:
        print(raw.decode('utf-8', errors='replace'))
    return ok


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--harness', type=Path, required=True)
    parser.add_argument('--app', type=Path)
    parser.add_argument('--qt-root', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--smoke', action='append', choices=SMOKES)
    args = parser.parse_args()
    environment = os.environ.copy()
    environment.update(QML_DISABLE_DISK_CACHE='1', QT_FORCE_STDERR_LOGGING='1',
                       QT_QUICK_CONTROLS_STYLE='Basic', COLOSSEUM_DISABLE_BOOT_PREFETCH='1')
    if args.qt_root:
        environment['PATH'] = str(args.qt_root / 'bin') + os.pathsep + environment.get('PATH', '')
        environment['QML_IMPORT_PATH'] = str(args.qt_root / 'qml') + os.pathsep + environment.get('QML_IMPORT_PATH', '')
    if args.output:
        args.output.mkdir(parents=True, exist_ok=True)
    source = Path(__file__).resolve().parent
    failures = 0
    with tempfile.TemporaryDirectory(prefix='feria-abc-', ignore_cleanup_errors=True) as directory:
        root = Path(directory)
        urls = root / 'urls.json'
        urls.write_text('["about:blank"]', encoding='utf-8')
        for name in args.smoke or SMOKES:
            command = [str(args.harness.resolve()), str(source / (name + '.qml')),
                       'navigation', str(urls), str(root / name)]
            failures += not run_check(command, environment, SMOKES[name], 55, name, args.output)
        if args.app:
            # Main refuses to execute its assertions unless the isolated tag is set.
            environment['COLOSSEUM_APPDATA_TAG'] = 'feria-abc-' + uuid.uuid4().hex
            command = [str(args.app.resolve()), str(source / 'FeriaMainShellSmoke.qml')]
            failures += not run_check(command, environment, 42, 80, 'FeriaMainShellSmoke', args.output)
    return 1 if failures else 0


if __name__ == '__main__':
    raise SystemExit(main())
