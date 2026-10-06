#!/usr/bin/env python3
"""Assemble a relocatable Ubuntu 24.04 x86_64 QtWebEngine development candidate."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def verify_qml(root: Path, manifest: Path) -> None:
    material = "".join(f"{path.relative_to(root).as_posix()}\n{sha256(path)}\n"
                       for path in sorted(root.rglob("*")) if path.is_file())
    expected = f"schema=1\nqmlTreeSha256={hashlib.sha256(material.encode()).hexdigest()}\n"
    if manifest.read_text() != expected:
        raise RuntimeError("QML build mismatch: refusing mixed source and binary")


def parse_ldd(output: str) -> list[Path]:
    if "=> not found" in output:
        raise RuntimeError("Unresolved ELF dependency: " + output)
    return [Path(match.group(1)) for match in
            re.finditer(r"(?:=>\s+|^\s*)(/\S+)\s+\(", output, re.MULTILINE)]


def verify_payload(root: Path) -> None:
    required = ["usr/bin/colosseum", "usr/libexec/QtWebEngineProcess",
                "usr/resources/qtwebengine_resources.pak",
                "usr/resources/qtwebengine_resources_100p.pak",
                "usr/resources/qtwebengine_resources_200p.pak",
                "usr/resources/icudtl.dat", "usr/resources/v8_context_snapshot.bin",
                "usr/translations/qtwebengine_locales/en-US.pak",
                "usr/plugins/platforms/libqxcb.so", "usr/plugins/sqldrivers/libqsqlite.so",
                "usr/qml/QtWebEngine/qmldir",
                "usr/bin/qml-build.manifest", "qml/Main.qml"]
    for name in required:
        if not (root / name).is_file():
            raise RuntimeError("Missing candidate runtime file: " + name)


def is_runtime_elf(path: Path) -> bool:
    """Select ET_EXEC/ET_DYN, never SDK ET_REL objects, for ldd.

    A corrupt ELF must fail qualification rather than silently escape the
    dependency audit. Non-ELF resources and directories are not candidates.
    Both packaging and clean-runtime qualification use this same predicate.
    """
    if not path.is_file():
        return False
    with path.open("rb") as stream:
        header = stream.read(64)
    if header[:4] != b"\x7fELF":
        return False
    if len(header) < 16 or header[4] not in (1, 2) or header[5] not in (1, 2) or header[6] != 1:
        raise RuntimeError(f"Malformed ELF identification: {path}")
    size, size_offset = (52, 40) if header[4] == 1 else (64, 52)
    order = "little" if header[5] == 1 else "big"
    if (len(header) < size
            or int.from_bytes(header[20:24], order) != 1
            or int.from_bytes(header[size_offset:size_offset + 2], order) != size):
        raise RuntimeError(f"Malformed ELF header: {path}")
    return int.from_bytes(header[16:18], order) in (2, 3)


# Qt SDK QML trees can contain CMake resource object outputs in addition to
# runtime plugins. These are neither shipped libraries nor input to ldd.
_sdk_development_ignores = shutil.ignore_patterns(
    "*.a", "*.o", "*.obj", "objects-*", "*.h", "*.cpp", "*.debug", "*.prl", "*.pdb")


def sdk_runtime_ignores(directory: str, names: list[str]) -> set[str]:
    ignored = set(_sdk_development_ignores(directory, names))
    if Path(directory).name == "plugins":
        # Qt Designer extension plugins are SDK authoring tools, not runtime
        # application plugins. They can depend on an uninstalled Designer SDK.
        ignored.update(name for name in names if name == "designer")
    if Path(directory).name == "sqldrivers":
        # Every native QSqlDatabase::addDatabase call uses QSQLITE. SDK-only
        # Oracle/MySQL/PostgreSQL/etc. plugins would introduce unused client
        # dependencies (including Oracle's proprietary libclntsh).
        ignored.update(name for name in names if not name.startswith("libqsqlite.so"))
    if Path(directory).name == "position":
        # The desktop candidate does not consume serial GPS/NMEA devices.
        # Qt Positioning ships this optional backend even when QtSerialPort
        # is not installed. Keep GeoClue and all other positioning support.
        ignored.update(name for name in names if name == "libqtposition_nmea.so")
    return ignored


# Keep the host's glibc and graphics drivers. All other resolved dependencies,
# including libmpv, libxcb-cursor and MpvQt, are carried in the candidate.
HOST_LIBS = re.compile(r"^(ld-linux.*|lib(c|m|pthread|rt|dl|resolv|util|nss_.*)\.so.*)$")


LAUNCHER = '''#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export LD_LIBRARY_PATH="$root/usr/lib"
export QT_PLUGIN_PATH="$root/usr/plugins"
export QML_IMPORT_PATH="$root/usr/qml"
export QML2_IMPORT_PATH="$root/usr/qml"
export QTWEBENGINEPROCESS_PATH="$root/usr/libexec/QtWebEngineProcess"
export QTWEBENGINE_RESOURCES_PATH="$root/usr/resources"
export QTWEBENGINE_LOCALES_PATH="$root/usr/translations/qtwebengine_locales"
cd "$root"
exec "$root/usr/bin/colosseum" "$@"
'''


def assemble(repo: Path, build: Path, qt: Path, mpvqt: Path, output: Path) -> Path:
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=repo, text=True).strip()
    if subprocess.check_output(["git", "status", "--porcelain", "--untracked-files=no"], cwd=repo):
        raise RuntimeError("Refusing dirty tracked source")
    if output.exists():
        raise RuntimeError("Output already exists; use a fresh staging directory")
    root = output / f"Colosseum-linux-qtwebengine-{revision[:12]}"
    binary = root / "usr/bin"
    library = root / "usr/lib"
    binary.mkdir(parents=True)
    library.mkdir(parents=True)
    for name in ("colosseum", "qml-build.manifest"):
        shutil.copy2(build / name, binary / name)
    tracked = subprocess.check_output(
        ["git", "ls-files", "-z", "qml", "assets", "resources", "data", "LICENSE"], cwd=repo)
    for name in tracked.decode().split("\0"):
        if name:
            target = root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(repo / name, target)
    verify_qml(root / "qml", binary / "qml-build.manifest")
    for name in ("plugins", "qml", "resources", "translations"):
        shutil.copytree(qt / name, root / "usr" / name, ignore=sdk_runtime_ignores)
    (root / "usr/libexec").mkdir()
    shutil.copy2(qt / "libexec/QtWebEngineProcess", root / "usr/libexec/QtWebEngineProcess")
    for prefix in (qt, mpvqt):
        for source in (prefix / "lib").glob("*.so*"):
            shutil.copy2(source, library / source.name, follow_symlinks=False)
    # ldd resolves the transitive dependencies of each bundled ELF; the queue
    # also covers plugins loaded dynamically, not just the main executable.
    env = dict(os.environ, LD_LIBRARY_PATH=f"{library}:{qt / 'lib'}:{mpvqt / 'lib'}")
    queue = [path for path in root.rglob("*") if is_runtime_elf(path)]
    visited: set[Path] = set()
    while queue:
        source = queue.pop()
        real = source.resolve()
        if real in visited:
            continue
        visited.add(real)
        result = subprocess.run(["ldd", str(source)], env=env, text=True, capture_output=True, check=True)
        try:
            dependencies = parse_ldd(result.stdout)
        except RuntimeError as error:
            raise RuntimeError(f"Dependency scan failed for {source}: {error}") from error
        for dependency in dependencies:
            if HOST_LIBS.match(dependency.name):
                continue
            target = library / dependency.name
            if not target.exists():
                shutil.copy2(dependency, target)
                queue.append(target)
    (binary / "qt.conf").write_text("[Paths]\nPrefix=..\nLibraries=lib\nPlugins=plugins\nQmlImports=qml\nLibraryExecutables=libexec\nData=.\nTranslations=translations\n")
    (root / "AppRun").write_text(LAUNCHER)
    (root / "AppRun").chmod(0o755)
    version = subprocess.check_output([str(qt / "bin/qmake"), "-query", "QT_VERSION"], text=True).strip()
    (root / "BUILD-INFO.json").write_text(json.dumps({
        "schema": 1, "sourceRevision": revision, "qtVersion": version,
        "engine": "QtWebEngine", "platform": "Ubuntu 24.04 x86_64",
        "workflowRun": os.environ.get("GITHUB_RUN_ID", "local"),
        "workflowAttempt": os.environ.get("GITHUB_RUN_ATTEMPT", "1"),
        "qualification": "candidate; inspect separate runtime evidence",
        "unverified": ["DRM", "proprietary codecs", "live providers", "Windows", "Theatre streaming/DVR"],
    }, indent=2) + "\n")
    (root / "README.txt").write_text(
        "QtWebEngine development candidate, not a release.\n"
        "Ubuntu 24.04 x86_64 or newer compatible glibc; host graphics drivers required.\n"
        "Extract the whole directory, then run ./AppRun. No Qt SDK or source checkout needed.\n"
        "Qt is bundled; this does not establish codecs, Widevine/DRM or provider playback.\n"
        "Theatre stream-server and external media helpers are outside this Feria candidate.\n"
        "Use BUILD-INFO.json and the archive checksum to identify the exact build.\n")
    verify_payload(root)
    files = sorted(path for path in root.rglob("*") if path.is_file())
    (root / "SHA256SUMS").write_text("".join(f"{sha256(path)}  {path.relative_to(root).as_posix()}\n" for path in files))
    archive = output / f"{root.name}.tar.gz"
    with tarfile.open(archive, "w:gz", compresslevel=6) as stream:
        stream.add(root, arcname=root.name)
    (output / f"{archive.name}.sha256").write_text(f"{sha256(archive)}  {archive.name}\n")
    print(f"CANDIDATE_PATH={archive}\nCANDIDATE_SHA256={sha256(archive)}\nSOURCE_REVISION={revision}")
    return archive


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path.cwd())
    for name in ("build", "qt", "mpvqt", "output"):
        parser.add_argument("--" + name, required=True, type=Path)
    args = parser.parse_args()
    if sys.platform != "linux":
        parser.error("Linux packaging requires a Linux build host")
    assemble(args.repo.resolve(), args.build.resolve(), args.qt.resolve(), args.mpvqt.resolve(), args.output.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
