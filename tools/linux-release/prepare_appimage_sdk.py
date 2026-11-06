#!/usr/bin/env python3
"""Adapt the full conda-forge C++ SDK to the existing assembler, retaining notices."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
from urllib.parse import urlparse

from package_appimage import REQUIRED_NOTICES


def require_glib_headers(prefix: Path) -> None:
    # libglib is a runtime split; its recipe removes lib/glib-* development data.
    for relative in ("include/glib-2.0/glib.h", "lib/glib-2.0/include/glibconfig.h"):
        if not (prefix / relative).is_file():
            raise ValueError("Full GLib development package required; missing " + relative)


def prepare(prefix: Path, overlay: Path, notices: Path) -> None:
    if overlay.exists() or notices.exists():
        raise ValueError("SDK overlay and notices destinations must be new")
    require_glib_headers(prefix)
    qmake = next((p for p in (prefix / "bin/qmake6", prefix / "bin/qmake",
                              prefix / "lib/qt6/bin/qmake") if p.is_file()), None)
    if qmake is None:
        raise ValueError("Full Qt C++ SDK qmake missing; a Python binding runtime is insufficient")
    query = subprocess.check_output([str(qmake), "-query"], text=True)
    paths = dict(line.split(":", 1) for line in query.splitlines() if ":" in line)
    if paths.get("QT_VERSION") != "6.11.1":
        raise ValueError("Expected Qt 6.11.1, found " + paths.get("QT_VERSION", "unknown"))
    mapping = {"lib": "QT_INSTALL_LIBS", "qml": "QT_INSTALL_QML",
               "plugins": "QT_INSTALL_PLUGINS", "translations": "QT_INSTALL_TRANSLATIONS",
               "libexec": "QT_INSTALL_LIBEXECS"}
    overlay.mkdir(parents=True)
    for name, key in mapping.items():
        target = Path(paths[key]).resolve(strict=True)
        if not target.is_relative_to(prefix.resolve()):
            raise ValueError("Qt SDK path escaped the selected prefix: " + str(target))
        (overlay / name).symlink_to(target, target_is_directory=True)
    resources = next((Path(paths[key]) / "resources" for key in ("QT_INSTALL_ARCHDATA", "QT_INSTALL_DATA")
                      if (Path(paths[key]) / "resources/icudtl.dat").is_file()), None)
    if resources is None or not resources.resolve().is_relative_to(prefix.resolve()):
        raise ValueError("Qt WebEngine resources missing from the selected full SDK")
    (overlay / "resources").symlink_to(resources, target_is_directory=True)
    if not (overlay / "libexec/QtWebEngineProcess").is_file():
        raise ValueError("Qt WebEngine helper missing")
    if not (overlay / "qml/QtQuick3D/qmldir").is_file():
        raise ValueError("Qt Quick 3D module missing")
    notices.mkdir(parents=True)
    records = []
    for metadata in sorted((prefix / "conda-meta").glob("*.json")):
        package = json.loads(metadata.read_text())
        name = package["name"]
        if name.startswith("qt6-") and package["version"] != "6.11.1":
            raise ValueError("Mixed Qt module versions: " + name)
        source = Path(package.get("link", {}).get("source", ""))
        if not source.is_dir() or not (source / "info/index.json").is_file():
            raise ValueError("Package cache provenance unavailable for " + name)
        url = package.get("url", "")
        parsed = urlparse(url)
        if parsed.scheme != "https" or parsed.hostname != "conda.anaconda.org" or parsed.username or parsed.password:
            raise ValueError("Non-public or unexpected package origin for " + name)
        digest = package.get("sha256", "")
        if not re.fullmatch(r"[0-9a-f]{64}", digest):
            raise ValueError("Package SHA256 unavailable for " + name)
        destination = notices / "conda" / metadata.stem
        destination.mkdir(parents=True)
        license_files = []
        for item in sorted((source / "info/licenses").rglob("*")):
            if not item.is_file():
                continue
            if not item.resolve().is_relative_to(source.resolve()):
                raise ValueError("Escaping package notice for " + name)
            target = destination / "licenses" / item.relative_to(source / "info/licenses")
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(item, target)
            license_files.append(target.relative_to(notices).as_posix())
        for item in ("about.json", "index.json", "hash_input.json"):
            if (source / "info" / item).is_file():
                shutil.copy2(source / "info" / item, destination / item)
        if (source / "info/recipe").is_dir():
            shutil.copytree(source / "info/recipe", destination / "recipe")
        records.append({"name": name, "version": package["version"], "build": package["build"],
                        "url": url, "sha256": digest, "license": package.get("license"),
                        "license_files": license_files})
    for name in REQUIRED_NOTICES:
        found = [record for record in records if record["name"] == name]
        if len(found) != 1 or not found[0]["license_files"]:
            raise ValueError("Unchanged license files missing for required package " + name)
    manifest = {"qt_version": "6.11.1", "origin": "conda-forge full C++ SDK, not PySide/PyQt",
                "packages": records, "scope": "Installed build dependency inventory; ELF closure identifies shipped libraries"}
    (notices / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (notices / "NOTICE.txt").write_text(
        "Colosseum original source is MIT; that does not describe this combined binary distribution.\n"
        "Unchanged notices and source/build recipes from the exact conda-forge packages are retained.\n"
        "Qt core includes LGPL terms; Qt Quick 3D carries GPL-3.0-only terms in this open-source build.\n"
        "mpv, FFmpeg and their enabled dependencies carry their own GPL/LGPL and other terms.\n"
        "See manifest.json, individual license files, and ffmpeg-license.txt for the actual builds.\n"
        "OpenSSL notices are included with its exact package. Host dependency notices are a superset.\n"
        "This review candidate is not a certification of redistribution compliance.\n"
        "Corresponding-source delivery, relinking and all license-specific obligations require review before release.\n")
    print(json.dumps({"qt": paths["QT_VERSION"], "packages": len(records), "notices": str(notices)}))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", required=True, type=Path)
    parser.add_argument("--overlay", required=True, type=Path)
    parser.add_argument("--notices", required=True, type=Path)
    args = parser.parse_args()
    prepare(args.prefix.resolve(), args.overlay.resolve(), args.notices.resolve())


if __name__ == "__main__":
    main()
