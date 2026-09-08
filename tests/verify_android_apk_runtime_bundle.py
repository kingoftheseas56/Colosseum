from __future__ import annotations

import os
from pathlib import Path
import sys
import zipfile


REQUIRED_FILES = (
    "assets/colosseum-runtime/qml/Main.qml",
    "assets/colosseum-runtime/qml-build.manifest",
    "assets/colosseum-runtime/runtime-files.manifest",
)
REQUIRED_PREFIXES = (
    "assets/colosseum-runtime/assets/",
    "assets/colosseum-runtime/resources/",
)


def apk_path() -> Path:
    if len(sys.argv) > 1:
        return Path(sys.argv[1]).resolve()
    override = os.environ.get("COLOSSEUM_ANDROID_APK")
    if override:
        return Path(override).resolve()
    return (
        Path(__file__).resolve().parents[1]
        / "native"
        / "build-android-arm64"
        / "android-build"
        / "colosseum.apk"
    )


def main() -> int:
    apk = apk_path()
    if not apk.is_file():
        print(f"ANDROID_RUNTIME_BUNDLE_APK_MISSING: {apk}", file=sys.stderr)
        return 2

    with zipfile.ZipFile(apk) as archive:
        entries = set(archive.namelist())
        missing_files = [name for name in REQUIRED_FILES if name not in entries]
        runtime_manifest = (
            archive.read("assets/colosseum-runtime/runtime-files.manifest").decode("utf-8")
            if not missing_files
            else ""
        )

    manifest_entries = [
        line[5:].split("\t", 1)[0]
        for line in runtime_manifest.splitlines()
        if line.startswith("file=")
    ]
    missing_manifest_entries = [
        relative
        for relative in manifest_entries
        if "assets/colosseum-runtime/" + relative not in entries
    ]
    missing_prefixes = [
        prefix for prefix in REQUIRED_PREFIXES if not any(name.startswith(prefix) for name in entries)
    ]

    if missing_files or missing_prefixes or missing_manifest_entries:
        for name in missing_files:
            print(f"MISSING_FILE {name}", file=sys.stderr)
        for prefix in missing_prefixes:
            print(f"MISSING_PREFIX {prefix}", file=sys.stderr)
        for relative in missing_manifest_entries:
            print(f"MANIFEST_ENTRY_MISSING {relative}", file=sys.stderr)
        return 1

    print(f"ANDROID_RUNTIME_BUNDLE_OK {apk}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
