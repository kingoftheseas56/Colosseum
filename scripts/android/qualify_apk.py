#!/usr/bin/env python3
"""Android APK artifact qualification for the Beta lane (WT-12).

Adapted from Alpha scripts/android/release_apk.py. This tool never builds, signs, installs,
publishes or dispatches CI. It records honest provenance for an APK that was already built by
scripts/android/build_apk.sh (or a local lane):

  inspect     structural ZIP/ELF checks: unique EOCD, no duplicate members, arm64-v8a-only
              native libraries that are AArch64 ELF64 with >=16 KiB PT_LOAD alignment, optional
              Nuvio engine member hash against the verified prefix, and the signing state
              (APK Signing Block / v1 JAR members) reported - or rejected with --require-unsigned.
  identity    parse `aapt dump badging` output and compare package/versionCode/versionName with
              the expected identity; the package must be inside the Colosseum lanes (default:
              the authorized Beta world.getcolosseum.app.beta, see native/cmake/AndroidPackageIdentity.cmake).
  snapshot    source provenance: commit, tree, index digest, and - instead of pretending the
              tracked tree alone describes the artifact - the list and digest of dirty tracked
              files plus an optional overlay manifest ({"files":[{"path","sha256"}]}) whose
              hashes must match the composed working tree.
  provenance  combine an inspect receipt, identity, before/after snapshots and the APK hash.

A cloud/debug-signed APK is NOT update-compatible with the installed phone's signer; the
receipt states the signer status it observed and never claims update compatibility.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import subprocess
import sys
import zipfile
from pathlib import Path

BETA_PACKAGE = "world.getcolosseum.app.beta"
APK_SIG_MAGIC = b"APK Sig Block 42"
ENGINE_MEMBER = "lib/arm64-v8a/libnuvio_engine.so"
PACKAGE_LANES = (
    re.compile(r"world[.]getcolosseum[.]app([.][a-z][a-z0-9_]*)?"),
    re.compile(r"org[.]qtproject[.]example([.]colosseum([.][a-z][a-z0-9_]*)?)?"),
)
MAX_VERSION_CODE = 2_100_000_000


# ---- identity -------------------------------------------------------------------------

def validate_package(package: str) -> None:
    if not re.fullmatch(r"[a-z][a-z0-9_]*([.][a-z][a-z0-9_]*)+", package or ""):
        raise ValueError(f"invalid Android package id: {package!r}")
    if not any(lane.fullmatch(package) for lane in PACKAGE_LANES):
        raise ValueError(f"package {package!r} is outside the Colosseum lanes")


def validate_version_code(raw) -> int:
    if not re.fullmatch(r"[1-9][0-9]*", str(raw)):
        raise ValueError("version code must be a positive decimal integer")
    value = int(raw)
    if value > MAX_VERSION_CODE:
        raise ValueError(f"version code must be <= {MAX_VERSION_CODE}")
    return value


def parse_aapt_identity(output: str) -> dict:
    match = re.search(r"^package: name='([^']+)' versionCode='([0-9]+)' versionName='([^']*)'",
                      output, re.MULTILINE)
    if not match:
        raise ValueError("aapt did not report a parseable package identity")
    return {"package": match.group(1), "versionCode": int(match.group(2)),
            "versionName": match.group(3)}


def check_identity(identity: dict, package: str, version_code=None, version_name=None) -> dict:
    validate_package(package)
    if identity["package"] != package:
        raise ValueError(f"APK package {identity['package']!r} != expected {package!r}")
    validate_version_code(identity["versionCode"])
    if version_code is not None and identity["versionCode"] != validate_version_code(version_code):
        raise ValueError(f"APK versionCode {identity['versionCode']} != expected {version_code}")
    if version_name is not None and identity["versionName"] != version_name:
        raise ValueError(f"APK versionName {identity['versionName']!r} != expected {version_name!r}")
    return identity


# ---- ZIP / ELF inspection (from Alpha release_apk.py) -----------------------------------

def find_eocd(data: bytes) -> tuple[int, int, int]:
    start = max(0, len(data) - (65535 + 22))
    found = []
    pos = data.rfind(b"PK\x05\x06", start)
    while pos >= start:
        if pos + 22 <= len(data):
            comment = struct.unpack_from("<H", data, pos + 20)[0]
            if pos + 22 + comment == len(data):
                found.append(pos)
        pos = data.rfind(b"PK\x05\x06", start, pos)
    if len(found) != 1:
        raise ValueError("APK has no unique valid ZIP EOCD")
    pos = found[0]
    disk, cd_disk, disk_entries, entries, size, offset = struct.unpack_from("<HHHHII", data, pos + 4)
    if disk or cd_disk or disk_entries != entries:
        raise ValueError("multi-disk ZIP is unsupported")
    if entries == 0xFFFF or size == 0xFFFFFFFF or offset == 0xFFFFFFFF:
        raise ValueError("ZIP64 APK is unsupported by this verifier")
    if offset + size != pos:
        raise ValueError("ZIP central directory bounds are inconsistent")
    return pos, offset, size


def has_apk_signing_block(data: bytes, central_offset: int) -> bool:
    if central_offset < 24:
        return False
    footer = data[central_offset - 24:central_offset]
    if footer[8:] != APK_SIG_MAGIC:
        return False
    size = struct.unpack_from("<Q", footer, 0)[0]
    start = central_offset - size - 8
    if size < 24 or start < 0 or start + 8 > central_offset - 24:
        raise ValueError("malformed APK Signing Block size")
    if struct.unpack_from("<Q", data, start)[0] != size:
        raise ValueError("APK Signing Block size fields disagree")
    return True


def has_jar_signatures(names: list[str]) -> bool:
    for name in names:
        upper = name.upper()
        if upper.startswith("META-INF/"):
            leaf = upper.rsplit("/", 1)[-1]
            if leaf.endswith((".SF", ".RSA", ".DSA", ".EC")) or leaf.startswith("SIG-"):
                return True
    return False


def _elf_receipt(name: str, blob: bytes) -> dict:
    if len(blob) < 64 or blob[:4] != b"\x7fELF" or blob[4] != 2 or blob[5] != 1:
        raise ValueError(f"{name} is not little-endian ELF64")
    if struct.unpack_from("<H", blob, 18)[0] != 183:
        raise ValueError(f"{name} is not AArch64")
    phoff = struct.unpack_from("<Q", blob, 32)[0]
    phentsize, phnum = struct.unpack_from("<HH", blob, 54)
    if phentsize < 56 or phnum == 0 or phoff + phentsize * phnum > len(blob):
        raise ValueError(f"{name} has invalid ELF program headers")
    aligns = []
    for i in range(phnum):
        off = phoff + i * phentsize
        if struct.unpack_from("<I", blob, off)[0] == 1:
            aligns.append(struct.unpack_from("<Q", blob, off + 48)[0])
    if not aligns or any(value < 16384 for value in aligns):
        raise ValueError(f"{name} has PT_LOAD alignment below 16 KiB")
    return {"path": name, "bytes": len(blob), "sha256": hashlib.sha256(blob).hexdigest(),
            "ptLoadAlignments": aligns}


def inspect_apk(apk_path: str, engine_library: str | None = None,
                require_unsigned: bool = False) -> dict:
    path = Path(apk_path)
    data = path.read_bytes()
    _, cd_offset, _ = find_eocd(data)
    signing_block = has_apk_signing_block(data, cd_offset)
    external_v4 = Path(str(path) + ".idsig").exists() or path.with_suffix(".idsig").exists()
    try:
        with zipfile.ZipFile(path, "r") as zf:
            bad = zf.testzip()
            if bad:
                raise ValueError(f"corrupt ZIP member: {bad}")
            infos = zf.infolist()
            names = [item.filename for item in infos]
            if len(names) != len(set(names)):
                raise ValueError("duplicate ZIP member names are not accepted")
            if "AndroidManifest.xml" not in names:
                raise ValueError("APK lacks AndroidManifest.xml")
            jar_signatures = has_jar_signatures(names)
            abis, libs = set(), []
            for info in infos:
                if not info.filename.startswith("lib/"):
                    continue
                parts = info.filename.split("/")
                if len(parts) < 3 or not parts[1]:
                    raise ValueError(f"invalid native-library path: {info.filename}")
                abis.add(parts[1])
                if parts[1] != "arm64-v8a":
                    raise ValueError(f"unexpected native ABI in APK: {parts[1]}")
                if re.fullmatch(r"lib/[^/]+/[^/]+[.]so", info.filename):
                    libs.append(_elf_receipt(info.filename, zf.read(info)))
    except zipfile.BadZipFile as exc:
        raise ValueError(f"malformed APK ZIP: {exc}") from exc
    if abis != {"arm64-v8a"} or not libs:
        raise ValueError("APK must contain arm64-v8a native libraries only")
    signed = signing_block or jar_signatures or external_v4
    if require_unsigned and signed:
        raise ValueError("APK is signed but --require-unsigned was requested")
    engine = None
    if engine_library:
        engine_path = Path(engine_library)
        if not engine_path.is_file():
            raise ValueError("hardened Nuvio engine library is missing")
        engine_hash = hashlib.sha256(engine_path.read_bytes()).hexdigest()
        packaged = next((item for item in libs if item["path"] == ENGINE_MEMBER), None)
        if not packaged or packaged["sha256"] != engine_hash:
            raise ValueError("APK Nuvio engine member does not match the verified prefix")
        engine = {"path": ENGINE_MEMBER, "sha256": engine_hash}
    return {
        "zipValid": True, "abis": sorted(abis), "nativeLibraries": libs,
        "engineNativeLibrary": engine,
        "engineMemberPresent": any(item["path"] == ENGINE_MEMBER for item in libs),
        "signingBlockPresent": signing_block, "jarSignatureMembersPresent": jar_signatures,
        "externalV4SignaturePresent": external_v4,
        # Observed signing state only; update compatibility requires the installed signer.
        "signedState": "signed" if signed else "unsigned",
        "updateCompatibleWithInstalledSigner": "UNVERIFIED",
        "apkSha256": hashlib.sha256(data).hexdigest(), "apkBytes": len(data),
    }


# ---- source + overlay provenance -------------------------------------------------------

def _git(repo: Path, *args: str, binary: bool = False):
    result = subprocess.run(["git", "-C", str(repo), *args], check=True, capture_output=True,
                            text=not binary)
    return result.stdout if binary else result.stdout.strip()


def source_snapshot(repo: str, overlay_manifest: str | None = None) -> dict:
    root = Path(repo).resolve()
    listing = _git(root, "ls-files", "--stage", "-z", binary=True)
    status = _git(root, "status", "--porcelain=v1", "-z", "--untracked-files=no", binary=True)
    dirty = [entry[3:].decode("utf-8", "replace") for entry in status.split(b"\0") if len(entry) > 3]
    diff = _git(root, "diff", "--binary", "HEAD", "--", binary=True)
    snapshot = {
        "sourceCommit": _git(root, "rev-parse", "HEAD"),
        "sourceTreeOid": _git(root, "rev-parse", "HEAD^{tree}"),
        "sourceIndexMetadataSha256": hashlib.sha256(listing).hexdigest(),
        "trackedIndexRecordCount": len([r for r in listing.split(b"\0") if r]),
        "trackedSourceClean": not dirty,
        "dirtyTrackedFiles": sorted(dirty),
        "trackedDiffSha256": hashlib.sha256(diff).hexdigest(),
        "overlay": None,
    }
    if overlay_manifest:
        manifest = json.loads(Path(overlay_manifest).read_text(encoding="utf-8"))
        files = manifest.get("files", [])
        if not files:
            raise ValueError("overlay manifest lists no files")
        checked = []
        for entry in files:
            rel = str(entry.get("path", ""))
            if not rel or rel.startswith("/") or ".." in Path(rel).parts:
                raise ValueError(f"invalid overlay path: {rel!r}")
            actual = hashlib.sha256((root / rel).read_bytes()).hexdigest()
            if actual != entry.get("sha256"):
                raise ValueError(f"overlay file {rel} does not match its manifest hash")
            checked.append({"path": rel, "sha256": actual})
        snapshot["overlay"] = {"manifestSha256": hashlib.sha256(Path(overlay_manifest).read_bytes()).hexdigest(),
                               "files": checked}
    return snapshot


def compare_snapshots(before: dict, after: dict) -> None:
    keys = ("sourceCommit", "sourceTreeOid", "sourceIndexMetadataSha256", "trackedIndexRecordCount",
            "dirtyTrackedFiles", "trackedDiffSha256", "overlay")
    differences = {k: {"before": before.get(k), "after": after.get(k)} for k in keys if before.get(k) != after.get(k)}
    if differences:
        raise ValueError(f"source changed during the build: {differences}")


def provenance(apk: str, receipt: dict, identity: dict, before: dict, after: dict) -> dict:
    compare_snapshots(before, after)
    apk_hash = hashlib.sha256(Path(apk).read_bytes()).hexdigest()
    if receipt.get("apkSha256") != apk_hash:
        raise ValueError("inspection receipt does not match the APK")
    return {
        "schema": 1, "apk": Path(apk).name, "apkSha256": apk_hash, "apkBytes": receipt["apkBytes"],
        "identity": identity, "inspection": receipt,
        "source": after, "sourceComposition": "overlay" if after.get("overlay")
        else ("dirty-tracked" if not after.get("trackedSourceClean") else "clean-commit"),
        "deviceAcceptance": "LOCAL_PENDING",
    }


# ---- CLI -------------------------------------------------------------------------------

def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = p.add_subparsers(dest="cmd", required=True)
    i = sub.add_parser("inspect")
    i.add_argument("--apk", required=True)
    i.add_argument("--engine-library")
    i.add_argument("--require-unsigned", action="store_true")
    i.add_argument("--output", required=True)
    d = sub.add_parser("identity")
    d.add_argument("--aapt-output", required=True, help="file with `aapt dump badging` output")
    d.add_argument("--package", default=BETA_PACKAGE)
    d.add_argument("--version-code")
    d.add_argument("--version-name")
    d.add_argument("--output", required=True)
    s = sub.add_parser("snapshot")
    s.add_argument("--repo", default=str(Path(__file__).resolve().parents[2]))
    s.add_argument("--overlay-manifest")
    s.add_argument("--output", required=True)
    v = sub.add_parser("provenance")
    for name in ("apk", "inspection", "identity", "before", "after", "output"):
        v.add_argument("--" + name, required=True)
    args = p.parse_args(argv)
    try:
        if args.cmd == "inspect":
            result = inspect_apk(args.apk, args.engine_library, args.require_unsigned)
        elif args.cmd == "identity":
            result = check_identity(parse_aapt_identity(Path(args.aapt_output).read_text(encoding="utf-8")),
                                    args.package, args.version_code, args.version_name)
        elif args.cmd == "snapshot":
            result = source_snapshot(args.repo, args.overlay_manifest)
        else:
            load = lambda f: json.loads(Path(f).read_text(encoding="utf-8"))
            result = provenance(args.apk, load(args.inspection), load(args.identity), load(args.before), load(args.after))
    except ValueError as exc:
        print(f"APK_QUALIFICATION_FAIL {exc}", file=sys.stderr)
        return 2
    Path(args.output).write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"APK_QUALIFICATION_OK {args.cmd}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
