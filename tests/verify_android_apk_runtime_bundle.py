from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile
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


def apk_path(argument: str | None = None) -> Path:
    if argument:
        return Path(argument).resolve()
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
    parser = argparse.ArgumentParser()
    parser.add_argument("apk", nargs="?")
    parser.add_argument("--qmlcachegen", help="Compile packaged QML with the matching Qt host tool")
    args = parser.parse_args()
    apk = apk_path(args.apk)
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
        # Qt's OpenSSL plugin loads these at runtime; static archives linked into
        # libcolosseum do not supply the plugin's shared-library dependencies.
        abis = {
            name.split("/")[1] for name in entries
            if name.startswith("lib/") and name.count("/") == 2 and name.endswith(".so")
        }
        for abi in sorted(abis):
            for library in ("libcrypto_3.so", "libssl_3.so"):
                name = f"lib/{abi}/{library}"
                if name not in entries:
                    missing_files.append(name)

        qml_failures = []
        if args.qmlcachegen:
            with tempfile.TemporaryDirectory(prefix="colosseum-qml-check-") as directory:
                source = Path(directory) / "component.qml"
                output = Path(directory) / "component.qmlc"
                for name in sorted(entries):
                    if not name.startswith("assets/colosseum-runtime/qml/") or not name.endswith(".qml"):
                        continue
                    source.write_bytes(archive.read(name))
                    result = subprocess.run(
                        [args.qmlcachegen, "--only-bytecode", "-o", str(output), str(source)],
                        capture_output=True, text=True,
                    )
                    if result.returncode:
                        qml_failures.append(name)
                        print(f"QML_COMPILE_FAILED {name}\n{result.stderr}", file=sys.stderr)

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

    if missing_files or missing_prefixes or missing_manifest_entries or qml_failures:
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
