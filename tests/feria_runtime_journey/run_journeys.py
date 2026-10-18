"""Run the Feria browser-foundation journeys and report their process verdicts.

    python tests/feria_runtime_journey/run_journeys.py --exe <feria_foundation_runtime.exe>
        [--qt-bin <dir>] [--runtime-dir <dir>] [--repeat 5] [--out results.json]

--qt-bin runs against an installed Qt kit. --runtime-dir instead names one
complete deployed runtime that also holds the executable: Qt DLLs, plugins,
qml/, QtWebEngineProcess.exe, resources/ and translations/qtwebengine_locales/
(the codec-enabled deployment is such a directory). The engine helper process
is sandboxed and resolves its DLLs beside itself, so an engine DLL dropped next
to the executable with the rest left on PATH does not work. The harness logs
the module paths it actually loaded.

Each journey is one process that ends through the application's exit path, so
the exit code covers the scenario, the orderly engine shutdown and anything
that goes wrong while the process is torn down. Journeys:

  shutdown         several live pages (two at one URL, one private) with a
                   running script, an unanswered navigation and a download in
                   flight; repeated --repeat times.
  shutdown-red     the same with FERIA_SKIP_ORDERLY_SHUTDOWN=1: the harness
                   must then report engines left alive (exit 3). A crash or a
                   pass here means the control did not exercise the fence.
  retention        two processes over one disposable profile and one origin:
                   the first writes ordinary and private site data, the second
                   must find the ordinary data and none of the private data.

Only loopback fixtures and throwaway profiles are used.
"""
import argparse
import json
import os
from pathlib import Path
import re
import socket
import subprocess
import sys
import tempfile


def free_port():
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 0))
        return probe.getsockname()[1]


def run(exe, journey, root, env_extra, qt_bin, overlay=None, headless=True):
    env = os.environ.copy()
    env.update({'FERIA_JOURNEY': journey, 'QT_FORCE_STDERR_LOGGING': '1'})
    if headless:
        env['QT_QPA_PLATFORM'] = 'offscreen'   # no windows, no focus taken
    env.update(env_extra)
    if qt_bin:
        env['PATH'] = str(qt_bin) + os.pathsep + env['PATH']
    if overlay:
        env['PATH'] = str(overlay) + os.pathsep + env['PATH']
        env['QTWEBENGINEPROCESS_PATH'] = str(overlay / 'QtWebEngineProcess.exe')
        env['QTWEBENGINE_RESOURCES_PATH'] = str(overlay / 'resources')
        env['QTWEBENGINE_LOCALES_PATH'] = str(overlay / 'translations' / 'qtwebengine_locales')
        env['QML_IMPORT_PATH'] = str(overlay / 'qml')
        env['QT_PLUGIN_PATH'] = str(overlay)
    script = root / 'tests' / 'feria_runtime_journey' / 'Journey.qml'
    try:
        done = subprocess.run([str(exe), '--journey', str(script)], cwd=root, env=env,
                              capture_output=True, text=True, errors='replace', timeout=180)
        code, log = done.returncode, done.stdout + done.stderr
    except subprocess.TimeoutExpired as hung:
        code, log = 'timeout', (hung.stdout or '') + (hung.stderr or '')
    if isinstance(code, int) and code < 0:
        code += 1 << 32
    result = re.search(r'FERIA_JOURNEY_RESULT (\w+) ?(.*)', log)
    engines = re.search(r'FERIA_JOURNEY_EXIT (.*)', log)
    versions = re.search(r'Runtime versions: (.*)', log)
    modules = re.findall(r'Runtime module: (.*)', log)
    teardown = re.search(r'FERIA_JOURNEY_TEARDOWN (.*)', log)
    crash = re.findall(r'FERIA_CRASH (.*)', log)
    return {'journey': journey, 'exit': hex(code) if isinstance(code, int) and code > 255 else code,
            'result': result.group(1) if result else None,
            'detail': result.group(2).strip() if result else None,
            'engines': engines.group(1).strip() if engines else None,
            'teardown': teardown.group(1).strip() if teardown else None,
            'crash': [frame.strip() for frame in crash],
            'versions': versions.group(1).strip() if versions else None,
            'modules': [module.strip() for module in modules],
            'control': env_extra.get('FERIA_SKIP_ORDERLY_SHUTDOWN') == '1'}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe', required=True, type=Path)
    parser.add_argument('--qt-bin', type=Path)
    parser.add_argument('--runtime-dir', type=Path)
    parser.add_argument('--repeat', type=int, default=5)
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    rows = []
    overlay = args.runtime_dir
    for _ in range(args.repeat):
        rows.append(run(args.exe, 'shutdown', root, {}, args.qt_bin, overlay))
    rows.append(run(args.exe, 'shutdown', root, {'FERIA_SKIP_ORDERLY_SHUTDOWN': '1'}, args.qt_bin, overlay))
    with tempfile.TemporaryDirectory(prefix='feria-retention-', ignore_cleanup_errors=True) as profile:
        shared = {'FERIA_FIXTURE_ROOT': profile, 'FERIA_FIXTURE_PORT': str(free_port())}
        rows.append(run(args.exe, 'retention-write', root, shared, args.qt_bin, overlay))
        rows.append(run(args.exe, 'retention-read', root, shared, args.qt_bin, overlay))

    ok = True
    for row in rows:
        if row['control']:
            # Engines must be reported alive; a clean pass means no fence ran.
            row['pass'] = row['exit'] == 3
        else:
            row['pass'] = row['exit'] == 0 and row['result'] == 'PASS'
        ok = ok and row['pass']
        print('PASS' if row['pass'] else 'FAIL', row['journey'] + (' (red control)' if row['control'] else ''),
              'exit=%s' % row['exit'], row['detail'] or '', '|', row['engines'] or '',
              '| teardown', row['teardown'] or 'not reached')
        for frame in row['crash'][:24]:
            print('      crash', frame)
    if rows and rows[0]['versions']:
        print('Runtime:', rows[0]['versions'])
        for module in rows[0]['modules']:
            print('Loaded:', module)
    if args.out:
        args.out.write_text(json.dumps(rows, indent=2), encoding='utf-8')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
