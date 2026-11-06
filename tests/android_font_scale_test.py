#!/usr/bin/env python3
"""WT-04: build the font-scale QuickTest harness against the real PlatformRuntime and run it.

Usage: python3 tests/android_font_scale_test.py --qt-root <Qt 6.11.1 gcc_64>
Requires g++ and pkg-config for that Qt host. Exits non-zero on build or test failure.
"""
import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def tool(qt_root, name):
    for sub in ("libexec", "bin"):
        candidate = Path(qt_root) / sub / name
        if candidate.exists():
            return str(candidate)
    raise SystemExit(f"missing Qt host tool {name} under {qt_root}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--qt-root", required=True)
    args = parser.parse_args()
    qt = Path(args.qt_root)
    env = dict(os.environ)
    env["PKG_CONFIG_PATH"] = str(qt / "lib/pkgconfig") + os.pathsep + env.get("PKG_CONFIG_PATH", "")
    env["LD_LIBRARY_PATH"] = str(qt / "lib") + os.pathsep + env.get("LD_LIBRARY_PATH", "")
    env.setdefault("QT_QPA_PLATFORM", "offscreen")
    env.setdefault("QT_QUICK_BACKEND", "software")
    native = ROOT / "native"
    harness = ROOT / "tests/android_font_scale/harness.cpp"
    with tempfile.TemporaryDirectory(prefix="android-font-scale-") as tmp:
        work = Path(tmp)
        moc = tool(qt, "moc")
        subprocess.check_call([moc, str(harness), "-o", str(work / "harness.moc")], env=env)
        subprocess.check_call([moc, str(native / "platform/PlatformRuntime.h"),
                               "-o", str(work / "moc_PlatformRuntime.cpp")], env=env)
        flags = subprocess.check_output(
            ["pkg-config", "--cflags", "--libs", "Qt6Quick", "Qt6QuickTest", "Qt6Qml", "Qt6Gui"],
            env=env, text=True).split()
        binary = work / "android_font_scale"
        subprocess.check_call(["g++", "-std=c++17", "-fPIC", f"-I{native}", f"-I{work}",
                               str(harness), str(native / "platform/PlatformRuntime.cpp"),
                               str(native / "platform/PlatformKind.cpp"),
                               str(work / "moc_PlatformRuntime.cpp"), *flags, "-o", str(binary)],
                              env=env)
        result = subprocess.run([str(binary), "-input",
                                 str(ROOT / "tests/android_font_scale/tst_android_text_scaling.qml")],
                                env=env, cwd=ROOT)
        return result.returncode


if __name__ == "__main__":
    sys.exit(main())
