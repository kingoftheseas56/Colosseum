#!/usr/bin/env python3
"""Stage a Linux CI runtime for isolated validation, not production release."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess


def elf(path):
    try:
        with path.open("rb") as stream:
            return stream.read(4) == b"\x7fELF"
    except OSError:
        return False


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("source", "build", "qt", "mpvqt", "stage", "commit", "run-id"):
        parser.add_argument("--" + name, required=True)
    args = parser.parse_args()
    source, build, qt, mpvqt, stage = (
        Path(getattr(args, name)).resolve()
        for name in ("source", "build", "qt", "mpvqt", "stage")
    )
    if not re.fullmatch(r"[0-9a-f]{40}", args.commit):
        raise SystemExit("Full source commit required")
    if stage.exists():
        raise SystemExit("Choose a new validation stage")
    bindir, libdir = stage / "usr/bin", stage / "usr/lib"
    bindir.mkdir(parents=True)
    libdir.mkdir()
    for name in ("colosseum", "colosseum_qml_tests", "qml-build.manifest"):
        shutil.copy2(build / name, bindir / name)
    for name in ("qml", "assets", "resources", "data"):
        shutil.copytree(source / name, stage / name)
    shutil.copytree(source / "tests/qml", stage / "tests/qml")
    ignore = shutil.ignore_patterns("*.a", "*.o", "*.prl", "*.la", "*.debug",
                                   "*.cmake", "cmake", "pkgconfig", "objects-*")
    for name in ("lib", "plugins", "qml", "resources", "libexec", "translations"):
        if (qt / name).exists():
            shutil.copytree(qt / name, stage / "usr" / name,
                            symlinks=True, dirs_exist_ok=True, ignore=ignore)
    for path in (mpvqt / "lib").glob("libMpvQt.so*"):
        shutil.copy2(path, libdir / path.name)
    # Keep the host's C/C++ runtime and graphics-driver family coherent.
    system = re.compile(r"^(ld-linux|lib(c|m|dl|pthread|rt|resolv|util|anl|gcc_s|stdc\+\+)\.so|lib(GL|EGL|GLX|GLdispatch|OpenGL|drm|gbm|vulkan|va))")
    env = dict(os.environ, LD_LIBRARY_PATH=":".join(map(str, (
        libdir, qt / "lib", mpvqt / "lib"))))
    pending = [p for p in stage.rglob("*") if p.is_file() and elf(p)]
    seen, external = set(), set()
    while pending:
        path = pending.pop()
        if path in seen:
            continue
        seen.add(path)
        result = subprocess.run(["ldd", str(path)], env=env, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if "not found" in result.stdout:
            raise SystemExit(f"Unresolved dependency: {path}\n{result.stdout}")
        for line in result.stdout.splitlines():
            match = re.match(r"\s*(\S+) => (/.+?) \(", line)
            if not match:
                continue
            name, origin = match.groups()
            if system.match(name):
                external.add(name)
                continue
            target = libdir / name
            if not target.exists():
                shutil.copy2(origin, target)
                pending.append(target)
    qtconf = "[Paths]\nPrefix=..\nLibraries=lib\nPlugins=plugins\nQmlImports=qml\nLibraryExecutables=libexec\nData=.\nTranslations=translations\n"
    (bindir / "qt.conf").write_text(qtconf)
    (stage / "usr/libexec/qt.conf").write_text(qtconf)
    launcher = stage / "AppRun"
    launcher.write_text('''#!/bin/sh
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
unset QT_QPA_PLATFORM_PLUGIN_PATH QTDIR QT_HOME LD_PRELOAD
export LD_LIBRARY_PATH="$HERE/usr/lib"
export QT_PLUGIN_PATH="$HERE/usr/plugins"
export QML_IMPORT_PATH="$HERE/usr/qml"
export QML2_IMPORT_PATH="$HERE/usr/qml"
export QTWEBENGINEPROCESS_PATH="$HERE/usr/libexec/QtWebEngineProcess"
export QTWEBENGINE_RESOURCES_PATH="$HERE/usr/resources"
export QTWEBENGINE_LOCALES_PATH="$HERE/usr/translations/qtwebengine_locales"
cd "$HERE"
exec "$HERE/usr/bin/colosseum" "$@"
''')
    launcher.chmod(0o755)
    identity = {
        "purpose": "isolated Linux validation; not a release package",
        "source_commit": args.commit,
        "workflow_run_id": args.run_id,
        "qt": "6.11.1",
        "binary_sha256": digest(bindir / "colosseum"),
        "qml_tests_sha256": digest(bindir / "colosseum_qml_tests"),
        "external_system_libraries": sorted(external),
    }
    (stage / "BUILD-IDENTITY.json").write_text(json.dumps(identity, indent=2) + "\n")
    for name in ("LICENSE", "THIRD_PARTY_NOTICES.md"):
        shutil.copy2(source / name, stage / name)
    print(json.dumps(identity, indent=2))


if __name__ == "__main__":
    main()
