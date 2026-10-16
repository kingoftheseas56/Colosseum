from __future__ import annotations

import argparse
import hashlib
import re
import os
import posixpath
from pathlib import Path
import subprocess
import struct
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


RUNTIME_PREFIX = "assets/colosseum-runtime/"
QML_PREFIX = RUNTIME_PREFIX + "qml/"


def qml_policy_failures(files: dict[str, bytes]) -> list[str]:
    """Inspect every shipped source, including inactive components/selectors."""
    failures = []
    for name, data in sorted(files.items()):
        if not name.startswith(QML_PREFIX):
            continue
        relative = name[len(QML_PREFIX):]
        if (re.search(r"(^|/)(feria|player2|player2host)/", relative, re.I)
                or re.search(r"(^|/)Vault[^/]*\.(qml|js)$", relative)
                or relative in {"reader2/Paper.qml", "TrackerApprovalWebView.qml", "ExtensionsSetupSheet.qml"}):
            failures.append(f"FORBIDDEN_QML_FILE {relative}")
        if relative.endswith((".qml", ".js")) or relative.endswith("qmldir"):
            text = data.decode("utf-8-sig")
            if re.search(r"^\s*(?:\.import|import)\s+QtWebEngine\b", text, re.M):
                failures.append(f"FORBIDDEN_WEBENGINE_IMPORT {relative}")
            if re.search(r'^\s*(?:\.import|import)\s+[^\n]*["/](?:feria|player2|player2host)(?:/|")', text, re.M | re.I):
                failures.append(f"FORBIDDEN_DESKTOP_IMPORT {relative}")
            for imported in re.findall(r'^\s*(?:\.import|import)\s+["\']([^"\']+)["\']', text, re.M):
                if ":" in imported:
                    continue  # URL/module imports are resolved by Qt, not this tree.
                target = posixpath.normpath(posixpath.join(posixpath.dirname(name), imported))
                exists = target in files or any(path.startswith(target + "/") for path in files)
                if not exists:
                    failures.append(f"MISSING_RELATIVE_IMPORT {relative}: {imported}")
            if re.search(r"newVinyl", text, re.I):
                failures.append(f"FORBIDDEN_NEWVINYL_REFERENCE {relative}")
    return failures


def android_java_policy_failures(files: dict[str, bytes]) -> list[str]:
    """DEX class names are UTF-8 descriptors, also for otherwise unused classes."""
    failures = []
    for name, data in sorted(files.items()):
        if "org/colosseum/vault/" in name:
            failures.append(f"FORBIDDEN_VAULT_JAVA_FILE {name}")
        if name.endswith((".dex", ".class")) and (
                b"org/colosseum/vault/" in data or b"org.colosseum.vault." in data):
            failures.append(f"FORBIDDEN_VAULT_JAVA_REFERENCE {name}")
    return failures


def runtime_manifest_failures(files: dict[str, bytes]) -> list[str]:
    """Require exact file membership and hashes, not just one-way existence."""
    failures = []
    runtime = {name[len(RUNTIME_PREFIX):]: data for name, data in files.items()
               if name.startswith(RUNTIME_PREFIX) and not name.endswith("/")}
    manifest = runtime.get("runtime-files.manifest", b"").decode("utf-8")
    rows = [line[5:].split("\t") for line in manifest.splitlines() if line.startswith("file=")]
    expected = {}
    for row in rows:
        if len(row) != 2 or row[0] in expected:
            failures.append("INVALID_RUNTIME_MANIFEST_ENTRY " + repr(row))
            continue
        expected[row[0]] = row[1]
    actual = {name: hashlib.sha256(data).hexdigest() for name, data in runtime.items()
              if name != "runtime-files.manifest"}
    for name in sorted(actual.keys() | expected.keys()):
        if actual.get(name) != expected.get(name):
            failures.append(f"RUNTIME_MANIFEST_MISMATCH {name}")
    material = "".join(f"{name}\n{actual[name]}\n" for name in sorted(actual))
    if f"bundleSha256={hashlib.sha256(material.encode()).hexdigest()}" not in manifest.splitlines():
        failures.append("RUNTIME_BUNDLE_HASH_MISMATCH")
    qml = {name[4:]: digest for name, digest in actual.items() if name.startswith("qml/")}
    material = "".join(f"{name}\n{qml[name]}\n" for name in sorted(qml))
    qml_manifest = runtime.get("qml-build.manifest", b"").decode("utf-8")
    if f"qmlTreeSha256={hashlib.sha256(material.encode()).hexdigest()}" not in qml_manifest.splitlines():
        failures.append("QML_TREE_HASH_MISMATCH")
    return failures


def verify_elf_version_dependencies(data: bytes) -> None:
    """Read loader metadata through PT_LOAD, not potentially stale section headers."""
    if data[:6] != b"\x7fELF\x02\x01":
        raise ValueError("expected a little-endian ELF64 runtime library")
    phoff = struct.unpack_from("<Q", data, 32)[0]
    phsize, phcount = struct.unpack_from("<HH", data, 54)
    headers = [struct.unpack_from("<IIQQQQQQ", data, phoff + i * phsize)
               for i in range(phcount)]
    for header in headers:
        if header[0] == 1 and header[7] > 1 and header[2] % header[7] != header[3] % header[7]:
            raise ValueError("PT_LOAD file/virtual offsets violate segment alignment; Android maps the wrong bytes")

    def offset(address: int) -> int:
        for header in headers:
            if header[0] == 1 and header[3] <= address < header[3] + header[5]:
                return header[2] + address - header[3]
        raise ValueError(f"unmapped dynamic address {address:#x}")

    dynamic = next(header for header in headers if header[0] == 2)
    tags = {}
    needed = []
    for pos in range(dynamic[2], dynamic[2] + dynamic[5], 16):
        tag, value = struct.unpack_from("<qQ", data, pos)
        if tag == 0:
            break
        if tag == 1:
            needed.append(value)
        tags[tag] = value
    strings = offset(tags[5])

    def string(index: int) -> str:
        if index >= tags[10]:
            raise ValueError("dynamic string offset exceeds DT_STRSZ")
        end = data.index(b"\0", strings + index, strings + tags[10])
        return data[strings + index:end].decode("utf-8")

    libraries = {string(index) for index in needed}
    if 0x6ffffffe not in tags:
        return
    cursor = offset(tags[0x6ffffffe])
    for _ in range(tags[0x6fffffff]):
        _, _, filename, _, next_entry = struct.unpack_from("<HHIII", data, cursor)
        name = string(filename)
        if name not in libraries:
            raise ValueError(f"version-needed library {name!r} absent from DT_NEEDED {sorted(libraries)}")
        cursor += next_entry


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
        runtime_files = {name: archive.read(name) for name in sorted(entries)
                         if name.startswith(RUNTIME_PREFIX) and not name.endswith("/")}
        java_files = {name: archive.read(name) for name in sorted(entries)
                      if name.endswith((".dex", ".class")) or "org/colosseum/vault/" in name}
        policy_failures = android_java_policy_failures(java_files)
        policy_failures.extend(qml_policy_failures(runtime_files))
        policy_failures.extend(runtime_manifest_failures(runtime_files))
        policy_failures.extend(f"FORBIDDEN_WEBENGINE_LIBRARY {name}" for name in sorted(entries)
                               if name.endswith(".so") and "webengine" in name.lower())
        for failure in policy_failures:
            print(failure, file=sys.stderr)
        shipped_qml = [name for name in runtime_files if name.startswith(QML_PREFIX)]
        print(f"ANDROID_PACKAGED_QML_FILES {len(shipped_qml)}")
        # Shared shell identifiers are reported honestly; they are not equivalent
        # to importing a desktop implementation. Do not claim a zero-text grep.
        references = [name[len(QML_PREFIX):] for name in shipped_qml
                      if name.endswith((".qml", ".js"))
                      and b"feria" in runtime_files[name].lower()]
        if references:
            print("ANDROID_SHARED_SHELL_DESKTOP_REFERENCES " + ",".join(references))
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
        elf_failures = []
        for abi in sorted(abis):
            for library in ("libcrypto_3.so", "libssl_3.so"):
                name = f"lib/{abi}/{library}"
                if name not in entries:
                    missing_files.append(name)
                else:
                    try:
                        verify_elf_version_dependencies(archive.read(name))
                    except (ValueError, KeyError, StopIteration, struct.error) as error:
                        elf_failures.append(name)
                        print(f"ELF_LOADER_METADATA_INVALID {name}: {error}", file=sys.stderr)

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

    if missing_files or missing_prefixes or missing_manifest_entries or qml_failures or elf_failures or policy_failures:
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
