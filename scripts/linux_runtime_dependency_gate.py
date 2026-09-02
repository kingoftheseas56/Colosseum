#!/usr/bin/env python3
"""Verify Colosseum's Linux runtime dependency contract."""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Iterable


def appdir_library(appdir: Path, patterns: Iterable[str]) -> Path | None:
    lib = appdir / "usr" / "lib"
    roots = [lib, appdir / "usr" / "lib64"]
    if lib.is_dir():
        roots.extend(path for path in lib.iterdir() if path.is_dir())
    for root in roots:
        if not root.is_dir():
            continue
        for pattern in patterns:
            match = next((path for path in root.glob(pattern) if path.is_file()), None)
            if match is not None:
                return match
    return None


def host_library(sonames: Iterable[str], extra_dirs: list[Path]) -> Path | None:
    for directory in extra_dirs:
        for soname in sonames:
            match = next(directory.glob(f"{soname}*"), None) if directory.is_dir() else None
            if match is not None:
                return match
    try:
        output = subprocess.run(
            ["ldconfig", "-p"], text=True, capture_output=True, check=False
        ).stdout
    except OSError:
        output = ""
    for line in output.splitlines():
        for soname in sonames:
            if line.lstrip().startswith(soname + " ") and "=>" in line:
                return Path(line.split("=>", 1)[1].strip())
    return None


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--appdir", type=Path)
    parser.add_argument("--extra-lib-dir", action="append", default=[], type=Path)
    parser.add_argument("--require-dvr", action="store_true")
    args = parser.parse_args()

    if args.appdir:
        mode = "appdir"
        xcb = appdir_library(args.appdir, ["libxcb-cursor.so.0*"])
        libmpv = appdir_library(args.appdir, ["libmpv.so.2*", "libmpv.so*"])
        mpv = args.appdir / "usr" / "bin" / "mpv"
        mpv = mpv if mpv.is_file() and os.access(mpv, os.X_OK) else None
    else:
        mode = "host"
        xcb = host_library(["libxcb-cursor.so.0"], args.extra_lib_dir)
        libmpv = host_library(["libmpv.so.2", "libmpv.so"], args.extra_lib_dir)
        found_mpv = shutil.which("mpv")
        mpv = Path(found_mpv) if found_mpv else None

    print(f"mode={mode}")
    print(f"libxcb-cursor.so.0={'AVAILABLE ' + str(xcb) if xcb else 'MISSING'}")
    print(f"libmpv.so={'AVAILABLE ' + str(libmpv) if libmpv else 'MISSING'}")

    hard_missing = xcb is None or libmpv is None
    if mpv is not None:
        print(f"dvr_mpv=AVAILABLE {mpv}")
    elif args.require_dvr:
        print("dvr_mpv=MISSING (standalone mpv is required when DVR is claimed)")
        hard_missing = True
    else:
        print("dvr_mpv=FAIL_CLOSED (Player1 libmpv remains available; DVR requires standalone mpv)")

    print(f"VERDICT={'FAIL' if hard_missing else 'PASS'}")
    return 1 if hard_missing else 0


if __name__ == "__main__":
    sys.exit(main())
