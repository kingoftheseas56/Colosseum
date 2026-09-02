#!/usr/bin/env python3
"""Fail-fast qualification for the Colosseum Qt 6.11 Android toolchain."""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

QT_SERIES = (6, 11)
MIN_ANDROID_API = 28
COMPILE_ANDROID_API = 36
BUILD_TOOLS = "36.0.0"
NDK_REVISION = "27.2.12479018"
MIN_JDK = 21
MIN_CMAKE = (3, 22)
TARGET_ABI = "arm64-v8a"


@dataclass(frozen=True)
class Check:
    name: str
    ok: bool
    detail: str


def version_tuple(text: str) -> tuple[int, ...]:
    match = re.search(r"(\d+(?:\.\d+)+)", text)
    if not match:
        return ()
    return tuple(int(part) for part in match.group(1).split("."))


def java_major(text: str) -> int | None:
    match = re.search(r'version\s+"(\d+)(?:\.|\")', text)
    if not match:
        return None
    major = int(match.group(1))
    return int(text.split('"')[1].split('.')[1]) if major == 1 else major


def run_text(command: list[str]) -> tuple[int, str]:
    try:
        result = subprocess.run(
            command, capture_output=True, text=True, timeout=15, check=False
        )
    except (OSError, subprocess.SubprocessError) as exc:
        return 127, str(exc)
    output = "\n".join(part for part in (result.stdout, result.stderr) if part)
    return result.returncode, output.strip()


def executable(explicit: str | None, name: str) -> str | None:
    if explicit:
        path = Path(explicit).expanduser()
        return str(path) if path.exists() else None
    return shutil.which(name)


def qt_version_from_root(root: Path) -> tuple[int, ...]:
    for candidate in (root.parent.name, root.name):
        parsed = version_tuple(candidate)
        if parsed:
            return parsed
    return ()


def find_host_qt(target_root: Path, explicit: str | None) -> Path | None:
    if explicit:
        return Path(explicit).expanduser()
    for name in ("gcc_64", "msvc2022_64", "mingw_64"):
        candidate = target_root.parent / name
        if candidate.is_dir():
            return candidate
    return None


def first_existing(root: Path, relatives: Iterable[str]) -> Path | None:
    for relative in relatives:
        candidate = root / relative
        if candidate.exists():
            return candidate
    return None


def ndk_revision(root: Path) -> str | None:
    properties = root / "source.properties"
    if not properties.is_file():
        return None
    match = re.search(r"^Pkg\.Revision\s*=\s*(.+)$", properties.read_text(
        encoding="utf-8", errors="replace"), re.MULTILINE)
    return match.group(1).strip() if match else None


def check_command(name: str, path: str | None, minimum: tuple[int, ...] | None = None) -> Check:
    if not path:
        return Check(name, False, "executable not found")
    rc, output = run_text([path, "--version"])
    if rc != 0:
        return Check(name, False, f"{path}: version probe failed ({output})")
    found = version_tuple(output)
    if minimum and (not found or found < minimum):
        return Check(name, False, f"{found or 'unknown'} < required {minimum}")
    return Check(name, True, f"{path} ({'.'.join(map(str, found)) or 'version unknown'})")


def check_java(path: str | None) -> Check:
    if not path:
        return Check("JDK", False, "java executable not found")
    rc, output = run_text([path, "-version"])
    found = java_major(output)
    if rc != 0 or found is None:
        return Check("JDK", False, f"unable to parse java -version from {path}")
    if found < MIN_JDK:
        return Check("JDK", False, f"JDK {found} < required JDK {MIN_JDK}")
    return Check("JDK", True, f"JDK {found} at {path}")


def check_qt(target_root: Path, host_root: Path | None) -> list[Check]:
    checks: list[Check] = []
    qt_cmake = first_existing(target_root, ("bin/qt-cmake", "bin/qt-cmake.bat"))
    version = qt_version_from_root(target_root)
    checks.append(Check("Qt Android arm64 kit", bool(qt_cmake) and target_root.name == "android_arm64_v8a" and version[:2] == QT_SERIES,
                        f"root={target_root}, version={version or 'unknown'}, qt-cmake={qt_cmake or 'missing'}"))
    host_scan = None if host_root is None else first_existing(host_root, ("bin/qmlimportscanner", "bin/qmlimportscanner.exe"))
    checks.append(Check("Qt host kit", bool(host_scan), f"root={host_root or 'missing'}, qmlimportscanner={host_scan or 'missing'}"))
    return checks


def check_sdk(root: Path) -> list[Check]:
    platform = root / "platforms" / f"android-{COMPILE_ANDROID_API}" / "android.jar"
    build_tools = root / "build-tools" / BUILD_TOOLS
    aapt2 = first_existing(build_tools, ("aapt2", "aapt2.exe"))
    adb = first_existing(root, ("platform-tools/adb", "platform-tools/adb.exe"))
    return [
        Check("Android SDK platform", platform.is_file(), str(platform)),
        Check("Android build tools", bool(aapt2), str(aapt2 or build_tools)),
        Check("Android platform tools", bool(adb), str(adb or root / "platform-tools")),
    ]


def check_ndk(root: Path) -> Check:
    found = ndk_revision(root)
    return Check(
        "Android NDK",
        found == NDK_REVISION,
        f"root={root}, revision={found or 'unknown'}, required={NDK_REVISION}",
    )


def connected_arm64(adb: str, requested_serial: str | None) -> Check:
    rc, output = run_text([adb, "devices"])
    if rc != 0:
        return Check("Physical ARM64 device", False, f"adb devices failed: {output}")
    serials = [line.split()[0] for line in output.splitlines()[1:] if line.strip().endswith("device")]
    serial = requested_serial or (serials[0] if serials else None)
    if not serial:
        return Check("Physical ARM64 device", False, "no adb device in device state")
    rc, abis = run_text([adb, "-s", serial, "shell", "getprop", "ro.product.cpu.abilist"])
    ok = rc == 0 and TARGET_ABI in abis.split(",")
    return Check("Physical ARM64 device", ok, f"serial={serial}, abilist={abis or 'unknown'}")


def parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--qt-root", default=os.getenv("COLOSSEUM_QT_ANDROID_ROOT"))
    ap.add_argument("--host-qt-root", default=os.getenv("COLOSSEUM_QT_HOST_ROOT"))
    ap.add_argument("--sdk-root", default=os.getenv("ANDROID_SDK_ROOT") or os.getenv("ANDROID_HOME"))
    ap.add_argument("--ndk-root", default=os.getenv("ANDROID_NDK_ROOT"))
    ap.add_argument("--java", default=None)
    ap.add_argument("--cmake", default=None)
    ap.add_argument("--ninja", default=None)
    ap.add_argument("--adb", default=None)
    ap.add_argument("--require-device", action="store_true")
    ap.add_argument("--device", default=None)
    return ap


def main(argv: list[str] | None = None) -> int:
    args = parser().parse_args(argv)
    checks: list[Check] = []
    target_root = Path(args.qt_root).expanduser() if args.qt_root else Path("__missing_qt_android__")
    host_root = find_host_qt(target_root, args.host_qt_root) if args.qt_root else None
    checks.extend(check_qt(target_root, host_root))

    sdk_root = Path(args.sdk_root).expanduser() if args.sdk_root else Path("__missing_android_sdk__")
    checks.extend(check_sdk(sdk_root))
    ndk_root = Path(args.ndk_root).expanduser() if args.ndk_root else sdk_root / "ndk" / NDK_REVISION
    checks.append(check_ndk(ndk_root))

    java = executable(args.java, "java")
    if not java and os.getenv("JAVA_HOME"):
        java = str(first_existing(Path(os.environ["JAVA_HOME"]), ("bin/java", "bin/java.exe")) or "") or None
    checks.append(check_java(java))
    checks.append(check_command("CMake", executable(args.cmake, "cmake"), MIN_CMAKE))
    checks.append(check_command("Ninja", executable(args.ninja, "ninja")))

    adb = executable(args.adb, "adb")
    if not adb:
        candidate = first_existing(sdk_root, ("platform-tools/adb", "platform-tools/adb.exe"))
        adb = str(candidate) if candidate else None
    if args.require_device:
        if adb:
            checks.append(connected_arm64(adb, args.device))
        else:
            checks.append(Check("Physical ARM64 device", False, "adb executable not found"))

    print(f"Colosseum Android baseline: API {MIN_ANDROID_API}-{COMPILE_ANDROID_API}, {TARGET_ABI}, NDK {NDK_REVISION}, JDK {MIN_JDK}+")
    for check in checks:
        state = "PASS" if check.ok else "FAIL"
        print(f"[{state}] {check.name}: {check.detail}")
    failed = [check for check in checks if not check.ok]
    print(f"SUMMARY: {len(checks) - len(failed)}/{len(checks)} checks passed")
    return 0 if not failed else 2


if __name__ == "__main__":
    sys.exit(main())
