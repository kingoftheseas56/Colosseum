#!/usr/bin/env python3
"""Stage the actual Android bundle and compile its QML roots on host Qt.

This catches QML type/import failures before an APK build. It does not replace
Android runtime qualification; the probe documents its native registration mocks.
"""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile
from test_android_runtime_bundle import stage_runtime


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qt-root', type=Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='android-qml-preflight-') as directory:
        runtime = stage_runtime(Path(directory))
        return subprocess.run([
            sys.executable, str(Path(__file__).with_name('android_qml_engine_compile.py')),
            '--qt-root', str(args.qt_root), '--qml-root', str(runtime / 'qml'),
            '--loader-roots',
        ]).returncode


if __name__ == '__main__':
    raise SystemExit(main())
