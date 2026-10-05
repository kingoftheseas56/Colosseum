"""Run the production Feria toolbar through real QtWebEngine and pointer/key input."""
import argparse
import http.server
import os
from pathlib import Path
import subprocess
import threading

from browser_smoke_test import Fixture


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--harness', type=Path, required=True)
    parser.add_argument('--qt-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    environment = dict(os.environ, QT_FORCE_STDERR_LOGGING='1',
                       QML_DISABLE_DISK_CACHE='1', QT_QUICK_CONTROLS_STYLE='Basic',
                       COLOSSEUM_DISABLE_BOOT_PREFETCH='1')
    environment['PATH'] = str(args.qt_root / 'bin') + os.pathsep + environment.get('PATH', '')
    fixture = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Fixture)
    threading.Thread(target=fixture.serve_forever, daemon=True).start()
    environment['FERIA_BROWSER_TEST_BASE_URL'] = f'http://127.0.0.1:{fixture.server_port}/fixture/'
    environment['FERIA_BROWSER_TEST_OUTPUT'] = args.output.resolve().as_posix()
    source = Path(__file__).resolve().parents[1] / 'feria_toolbar_probe'
    try:
        result = subprocess.run([str(args.harness.resolve()), '-input', str(source),
                                 '-o', str(args.output / 'toolbar-results.txt') + ',txt'],
                                env=environment, capture_output=True, timeout=120)
        (args.output / 'toolbar-runtime.log').write_bytes(result.stdout + result.stderr)
        print((args.output / 'toolbar-results.txt').read_text(encoding='utf-8'))
        return result.returncode
    finally:
        fixture.shutdown()
        fixture.server_close()


if __name__ == '__main__':
    raise SystemExit(main())
