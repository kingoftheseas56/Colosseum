#!/usr/bin/env python3
"""Validated Android APK build modes and structural unsigned-APK verification."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import struct
import subprocess
import sys
import zipfile
from dataclasses import asdict, dataclass

DEFAULT_PACKAGE = "org.qtproject.example.colosseum.previewnuvio1"
DEFAULT_ENDPOINT = "https://colosseum-content-api-staging.colosseum-watchparty-relay.workers.dev"
PRODUCTION_PACKAGE = "world.getcolosseum.app"
PRODUCTION_ENDPOINT = "https://colosseum-content-api.colosseum-watchparty-relay.workers.dev"
SIGNING_ENV = ("COLOSSEUM_ANDROID_KEYSTORE", "COLOSSEUM_ANDROID_KEYSTORE_PASSWORD",
               "COLOSSEUM_ANDROID_KEY_ALIAS")
APK_SIG_MAGIC = b"APK Sig Block 42"
PROTECTED_CMAKE_KEYS = {
    "CMAKE_BUILD_TYPE", "QT_ANDROID_DEPLOYMENT_TYPE", "QT_ANDROID_SIGN_APK",
    "COLOSSEUM_ANDROID_NUVIO_BACKEND", "COLOSSEUM_ANDROID_NUVIO_X86_64_DIAGNOSTIC",
    "COLOSSEUM_NUVIO_ENGINE_PREFIX", "COLOSSEUM_NUVIO_DIAGNOSTIC_PACKAGE",
    "COLOSSEUM_ANDROID_PREVIEW_PACKAGE", "COLOSSEUM_ANDROID_VERSION_CODE",
    "COLOSSEUM_ANDROID_APPLICATION_ID", "COLOSSEUM_ANDROID_VERSION_NAME",
    "COLOSSEUM_CONTENT_API_URL",
}

@dataclass(frozen=True)
class BuildConfig:
    flavour: str
    build_dir: str
    artifact_name: str
    package: str = ""
    version_code: int = 0
    endpoint: str = ""
    engine_prefix: str = ""
    channel: str = "preview"
    version_name: str = ""

def _version_code(raw: str) -> int:
    if not re.fullmatch(r"[1-9][0-9]*", str(raw)):
        raise ValueError("version code must be a positive decimal integer")
    value = int(raw)
    if value > 2_100_000_000:
        raise ValueError("version code must be <= 2100000000")
    return value

def _reject_release_overrides(raw: str) -> None:
    try:
        args = shlex.split(raw)
    except ValueError as exc:
        raise ValueError(f"invalid COLOSSEUM_ANDROID_CMAKE_ARGS: {exc}") from exc
    for arg in args:
        if arg.startswith("-D"):
            key = arg[2:].split("=", 1)[0].split(":", 1)[0]
            if key in PROTECTED_CMAKE_KEYS:
                raise ValueError(f"release mode forbids overriding -D{key}")

def resolve_build_config(abi: str, build_root: str, out_dir: str,
                         env: dict[str, str] | None = None) -> BuildConfig:
    """Pure mode resolver; it does not inspect files or invoke tools."""
    env = dict(os.environ if env is None else env)
    flavour = env.get("COLOSSEUM_ANDROID_APK_FLAVOUR", "debug").strip()
    if abi not in ("arm64-v8a", "x86_64"):
        raise ValueError(f"unsupported ABI: {abi}")
    root = Path(build_root)
    if flavour == "debug":
        return BuildConfig(flavour, str(root / abi), f"colosseum-{abi}-debug.apk")
    if flavour != "release-unsigned":
        raise ValueError(f"unsupported APK flavour: {flavour}")
    if abi != "arm64-v8a":
        raise ValueError("release-unsigned is supported only for arm64-v8a")
    for name in SIGNING_ENV:
        if env.get(name, ""):
            raise ValueError(f"release-unsigned forbids signing environment variable {name}")
    channel = env.get("COLOSSEUM_ANDROID_RELEASE_CHANNEL", "preview").strip()
    if channel not in ("preview", "alpha"):
        raise ValueError("release channel must be preview or alpha")
    version_name = env.get("COLOSSEUM_ANDROID_VERSION_NAME", "").strip()
    application_id = env.get("COLOSSEUM_ANDROID_APPLICATION_ID", "").strip()
    preview = env.get("COLOSSEUM_ANDROID_PREVIEW_PACKAGE", "").strip()
    if channel == "alpha":
        package = application_id
        if package != PRODUCTION_PACKAGE or preview:
            raise ValueError("alpha release package must be world.getcolosseum.app with no preview package")
        if not re.fullmatch(r"[0-9]+[.][0-9]+[.][0-9]+-alpha", version_name):
            raise ValueError("alpha release requires an explicit <project-version>-alpha version name")
    else:
        package = preview or DEFAULT_PACKAGE
        if application_id or not re.fullmatch(r"org[.]qtproject[.]example[.]colosseum[.]preview[0-9a-z]+", package):
            raise ValueError("release package must be org.qtproject.example.colosseum.preview<id>")
        if version_name:
            raise ValueError("preview release uses the project version name")
    code = _version_code(env.get("COLOSSEUM_ANDROID_VERSION_CODE", "2"))
    endpoint = env.get("COLOSSEUM_CONTENT_API_URL", PRODUCTION_ENDPOINT if channel == "alpha" else DEFAULT_ENDPOINT).strip()
    if channel == "preview" and endpoint != DEFAULT_ENDPOINT:
        raise ValueError("release content endpoint must be the authorized HTTPS staging origin")
    if channel == "alpha" and endpoint not in (PRODUCTION_ENDPOINT, DEFAULT_ENDPOINT):
        raise ValueError("alpha content endpoint must be a first-party production or staging origin")
    if env.get("COLOSSEUM_NUVIO_DIAGNOSTIC_PACKAGE", "").strip():
        raise ValueError("release-unsigned forbids a diagnostic package")
    prefix = env.get("COLOSSEUM_NUVIO_ENGINE_PREFIX", "").strip()
    if not prefix:
        raise ValueError("release-unsigned requires COLOSSEUM_NUVIO_ENGINE_PREFIX")
    _reject_release_overrides(env.get("COLOSSEUM_ANDROID_CMAKE_ARGS", ""))
    return BuildConfig(flavour, str(root / "release-unsigned" / "arm64-v8a"),
                       "colosseum-arm64-v8a-release-unsigned.apk",
                       package, code, endpoint, prefix, channel, version_name)

def release_cmake_args(config: BuildConfig) -> list[str]:
    if config.flavour != "release-unsigned":
        raise ValueError("release CMake flags requested for non-release mode")
    return [
        "-DCMAKE_BUILD_TYPE=Release",
        "-DQT_ANDROID_DEPLOYMENT_TYPE:STRING=RELEASE",
        "-DQT_ANDROID_SIGN_APK:BOOL=OFF",
        "-DCOLOSSEUM_ANDROID_NUVIO_BACKEND=ON",
        "-DCOLOSSEUM_ANDROID_NUVIO_X86_64_DIAGNOSTIC=OFF",
        f"-DCOLOSSEUM_NUVIO_ENGINE_PREFIX={config.engine_prefix}",
        "-DCOLOSSEUM_NUVIO_DIAGNOSTIC_PACKAGE=",
        f"-DCOLOSSEUM_ANDROID_PREVIEW_PACKAGE={config.package if config.channel == 'preview' else ''}",
        f"-DCOLOSSEUM_ANDROID_APPLICATION_ID={config.package if config.channel == 'alpha' else ''}",
        f"-DCOLOSSEUM_ANDROID_VERSION_NAME={config.version_name}",
        f"-DCOLOSSEUM_ANDROID_VERSION_CODE={config.version_code}",
        f"-DCOLOSSEUM_CONTENT_API_URL={config.endpoint}",
    ]

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

def inspect_unsigned_zip(apk_path: str, engine_library: str | None = None) -> dict:
    path = Path(apk_path)
    if Path(str(path) + ".idsig").exists() or path.with_suffix(".idsig").exists():
        raise ValueError("APK has an external v4 .idsig signature file")
    data = path.read_bytes()
    _, cd_offset, _ = find_eocd(data)
    signed_block = has_apk_signing_block(data, cd_offset)
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
            jarsig = has_jar_signatures(names)
            abis, libs = set(), []
            for info in infos:
                if info.filename.startswith("lib/"):
                    parts = info.filename.split("/")
                    if len(parts) < 3 or not parts[1]:
                        raise ValueError(f"invalid native-library path: {info.filename}")
                    abi = parts[1]
                    abis.add(abi)
                    if abi != "arm64-v8a":
                        raise ValueError(f"unexpected native ABI in APK: {abi}")
                match = re.fullmatch(r"lib/([^/]+)/([^/]+[.]so)", info.filename)
                if not match:
                    continue
                abi, _ = match.groups()
                blob = zf.read(info)
                if len(blob) < 64 or blob[:4] != b"\x7fELF" or blob[4] != 2 or blob[5] != 1:
                    raise ValueError(f"{info.filename} is not little-endian ELF64")
                if struct.unpack_from("<H", blob, 18)[0] != 183:
                    raise ValueError(f"{info.filename} is not AArch64")
                phoff = struct.unpack_from("<Q", blob, 32)[0]
                phentsize, phnum = struct.unpack_from("<HH", blob, 54)
                if phentsize < 56 or phnum == 0 or phoff + phentsize * phnum > len(blob):
                    raise ValueError(f"{info.filename} has invalid ELF program headers")
                aligns = []
                for i in range(phnum):
                    off = phoff + i * phentsize
                    if struct.unpack_from("<I", blob, off)[0] == 1:
                        aligns.append(struct.unpack_from("<Q", blob, off + 48)[0])
                if not aligns or any(value < 16384 for value in aligns):
                    raise ValueError(f"{info.filename} has PT_LOAD alignment below 16 KiB")
                libs.append({"path": info.filename, "bytes": info.file_size,
                             "sha256": hashlib.sha256(blob).hexdigest(),
                             "ptLoadAlignments": aligns})
    except zipfile.BadZipFile as exc:
        raise ValueError(f"malformed APK ZIP: {exc}") from exc
    if signed_block:
        raise ValueError("APK contains an APK Signing Block (v2/v3 signing)")
    if jarsig:
        raise ValueError("APK contains JAR/v1 signing members")
    if abis != {"arm64-v8a"} or not libs:
        raise ValueError("APK must contain arm64-v8a native libraries only")
    engine_receipt = None
    if engine_library:
        engine_path = Path(engine_library)
        if not engine_path.is_file():
            raise ValueError("hardened Nuvio engine library is missing")
        engine_hash = hashlib.sha256(engine_path.read_bytes()).hexdigest()
        packaged = next((item for item in libs
                         if item["path"] == "lib/arm64-v8a/libnuvio_engine.so"), None)
        if not packaged or packaged["sha256"] != engine_hash:
            raise ValueError("APK Nuvio engine member does not match the verified prefix")
        engine_receipt = {"path": packaged["path"], "sha256": engine_hash}
    return {"zipValid": True, "signingBlockPresent": False,
            "jarSignatureMembersPresent": False, "abis": sorted(abis),
            "nativeLibraries": libs, "engineNativeLibrary": engine_receipt}

def parse_aapt_identity(output: str) -> dict:
    match = re.search(r"^package: name='([^']+)' versionCode='([0-9]+)' versionName='([^']*)'",
                      output, re.MULTILINE)
    if not match:
        raise ValueError("aapt did not report a parseable package identity")
    return {"package": match.group(1), "versionCode": int(match.group(2)),
            "versionName": match.group(3)}

def project_version(repo: str) -> str:
    cmake = Path(repo) / "native" / "CMakeLists.txt"
    source = cmake.read_text(encoding="utf-8", errors="replace")
    match = re.search(r"^\s*project\s*\(\s*colosseum\s+VERSION\s+([^\s)]+)",
                      source, re.MULTILINE | re.IGNORECASE)
    if not match:
        raise ValueError("native/CMakeLists.txt has no literal colosseum project VERSION")
    return match.group(1)

def run_unsigned_verification(apk: str, package: str, version_code: int,
                              aapt: str, zipalign: str,
                              engine_library: str, version_name: str) -> dict:
    receipt = inspect_unsigned_zip(apk, engine_library)
    run = subprocess.run([aapt, "dump", "badging", apk], check=False,
                         capture_output=True, text=True)
    if run.returncode:
        raise ValueError(f"aapt dump badging failed ({run.returncode})")
    identity = parse_aapt_identity(run.stdout)
    if (identity["package"] != package or identity["versionCode"] != version_code
            or identity["versionName"] != version_name):
        raise ValueError(f"APK identity mismatch: {identity}")
    aligned = subprocess.run([zipalign, "-c", "-P", "16", "4", apk],
                             check=False, capture_output=True, text=True)
    if aligned.returncode:
        raise ValueError(f"zipalign 16 KiB check failed ({aligned.returncode})")
    data = Path(apk).read_bytes()
    return {**receipt, **identity, "zipalign16KiB": True,
            "apkSha256": hashlib.sha256(data).hexdigest(), "apkBytes": len(data)}

def _cmake_cache(build_dir: Path) -> dict[str, str]:
    wanted = {
        "ANDROID_ABI", "CMAKE_BUILD_TYPE", "QT_ANDROID_DEPLOYMENT_TYPE",
        "QT_ANDROID_SIGN_APK", "COLOSSEUM_ANDROID_NUVIO_BACKEND",
        "COLOSSEUM_ANDROID_NUVIO_X86_64_DIAGNOSTIC",
        "COLOSSEUM_NUVIO_DIAGNOSTIC_PACKAGE", "COLOSSEUM_NUVIO_ENGINE_PREFIX",
        "COLOSSEUM_ANDROID_PREVIEW_PACKAGE",
        "COLOSSEUM_ANDROID_APPLICATION_ID", "COLOSSEUM_ANDROID_VERSION_NAME",
        "COLOSSEUM_ANDROID_VERSION_CODE", "COLOSSEUM_CONTENT_API_URL",
    }
    out = {}
    for line in (build_dir / "CMakeCache.txt").read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("//") or ":" not in line or "=" not in line:
            continue
        left, value = line.split("=", 1)
        key = left.split(":", 1)[0]
        if key in wanted:
            out[key] = value
    return out

def _source_snapshot(repo: str, environment: dict[str, str] | None = None) -> dict:
    repo_path = Path(repo).resolve()
    rev = subprocess.run(["git", "-C", str(repo_path), "rev-parse", "HEAD"],
                         check=True, capture_output=True, text=True).stdout.strip()
    tree = subprocess.run(["git", "-C", str(repo_path), "rev-parse", "HEAD^{tree}"],
                          check=True, capture_output=True, text=True).stdout.strip()
    tracked_diff = subprocess.run(["git", "-C", str(repo_path), "diff", "--quiet", "HEAD", "--"],
                                  check=False)
    if tracked_diff.returncode != 0:
        raise ValueError("release provenance requires clean tracked source files")
    untracked = subprocess.run([
        "git", "-C", str(repo_path), "ls-files", "--others", "--exclude-standard", "-z", "--",
        "native", "qml", "assets", "resources", "scripts", "tests", "extensions",
    ], check=True, capture_output=True).stdout
    if untracked:
        raise ValueError("release provenance rejects untracked build inputs; commit the explicit release source first")
    listing = subprocess.run(["git", "-C", str(repo_path), "ls-files", "--stage", "-z"],
                             check=True, capture_output=True).stdout
    index_records = [record for record in listing.split(b"\0") if record]
    env = os.environ if environment is None else environment
    branch = subprocess.run(["git", "-C", str(repo_path), "rev-parse", "--abbrev-ref", "HEAD"],
                            check=True, capture_output=True, text=True).stdout.strip()
    return {
        "sourceCommit": rev,
        "sourceTreeOid": tree,
        "sourceIndexMetadataSha256": hashlib.sha256(listing).hexdigest(),
        "trackedIndexRecordCount": len(index_records),
        "trackedSourceClean": True,
        "branchRef": env.get("GITHUB_REF") or branch,
        "runId": env.get("GITHUB_RUN_ID") or env.get("COLOSSEUM_ANDROID_RELEASE_RUN_ID", ""),
    }

def write_source_snapshot(repo: str, output: str) -> dict:
    result = _source_snapshot(repo)
    Path(output).write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return result

def verify_source_snapshots(repo: str, before_path: str, after_output: str) -> dict:
    before = json.loads(Path(before_path).read_text(encoding="utf-8"))
    after = _source_snapshot(repo)
    Path(after_output).write_text(json.dumps(after, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    comparable = ("sourceCommit", "sourceTreeOid", "sourceIndexMetadataSha256",
                  "trackedIndexRecordCount", "branchRef", "runId")
    differences = {key: {"before": before.get(key), "after": after.get(key)}
                   for key in comparable if before.get(key) != after.get(key)}
    if differences:
        raise ValueError(f"tracked source changed during release build: {differences}")
    return {"sourceCommit": after["sourceCommit"],
            "sourceTreeOid": after["sourceTreeOid"],
            "sourceIndexMetadataSha256": after["sourceIndexMetadataSha256"],
            "trackedIndexRecordCount": after["trackedIndexRecordCount"],
            "branchRef": after["branchRef"], "runId": after["runId"]}

def write_provenance(repo: str, build_dir: str, apk: str, verification: str,
                     engine_stamp: str, endpoint: str, output: str,
                     source_before: str, source_after: str,
                     branch_ref: str, run_id: str) -> dict:
    verified = json.loads(Path(verification).read_text(encoding="utf-8"))
    before = json.loads(Path(source_before).read_text(encoding="utf-8"))
    after = json.loads(Path(source_after).read_text(encoding="utf-8"))
    local_run = bool(re.fullmatch(r"local-[0-9]{8}T[0-9]{6}Z", run_id))
    run_environment = ({"COLOSSEUM_ANDROID_RELEASE_RUN_ID": run_id} if local_run else
                       {"GITHUB_REF": branch_ref, "GITHUB_RUN_ID": run_id})
    current = _source_snapshot(repo, run_environment)
    if not branch_ref or not (local_run or re.fullmatch(r"[0-9]+", run_id)):
        raise ValueError("provenance requires a branch/ref and numeric workflow or local UTC run ID")
    if local_run and branch_ref != current["branchRef"]:
        raise ValueError("local provenance branch/ref must match the source checkout")
    if not local_run and os.environ.get("GITHUB_ACTIONS") != "true":
        raise ValueError("numeric workflow provenance requires a GitHub Actions environment")
    fields = ("sourceCommit", "sourceTreeOid", "sourceIndexMetadataSha256",
              "trackedIndexRecordCount", "branchRef", "runId")
    if (any(before.get(key) != after.get(key) for key in fields)
            or any(after.get(key) != current.get(key) for key in fields)):
        raise ValueError("pre-build, post-build, and current source snapshots do not match")
    cache = _cmake_cache(Path(build_dir))
    required = {
        "ANDROID_ABI": "arm64-v8a", "CMAKE_BUILD_TYPE": "Release",
        "QT_ANDROID_DEPLOYMENT_TYPE": "RELEASE", "QT_ANDROID_SIGN_APK": "OFF",
        "COLOSSEUM_ANDROID_NUVIO_BACKEND": "ON",
        "COLOSSEUM_ANDROID_NUVIO_X86_64_DIAGNOSTIC": "OFF",
        "COLOSSEUM_NUVIO_DIAGNOSTIC_PACKAGE": "",
        "COLOSSEUM_ANDROID_VERSION_CODE": str(verified["versionCode"]),
        "COLOSSEUM_CONTENT_API_URL": endpoint,
    }
    permanent = verified["package"] == PRODUCTION_PACKAGE
    required["COLOSSEUM_ANDROID_PREVIEW_PACKAGE"] = "" if permanent else verified["package"]
    if permanent:
        required["COLOSSEUM_ANDROID_APPLICATION_ID"] = PRODUCTION_PACKAGE
        required["COLOSSEUM_ANDROID_VERSION_NAME"] = project_version(repo) + "-alpha"
    elif cache.get("COLOSSEUM_ANDROID_APPLICATION_ID", ""):
        raise ValueError("preview provenance cannot contain a permanent application id")
    mismatches = {k: {"expected": v, "actual": cache.get(k)}
                  for k, v in required.items() if cache.get(k) != v}
    if mismatches:
        raise ValueError(f"release CMake identity mismatch: {mismatches}")
    expected_version = project_version(repo) + ("-alpha" if permanent else "")
    if verified.get("versionName") != expected_version:
        raise ValueError("APK versionName does not match the project VERSION and release channel")
    if permanent and endpoint not in (PRODUCTION_ENDPOINT, DEFAULT_ENDPOINT):
        raise ValueError("alpha provenance requires a first-party content endpoint")
    if (not verified.get("zipValid") or verified.get("signingBlockPresent")
            or verified.get("jarSignatureMembersPresent")
            or not verified.get("engineNativeLibrary")):
        raise ValueError("unsigned verification receipt is not clean")
    apk_data = Path(apk).read_bytes()
    apk_hash = hashlib.sha256(apk_data).hexdigest()
    if verified.get("apkSha256") != apk_hash:
        raise ValueError("unsigned verification receipt does not match the APK")
    prefix = cache.get("COLOSSEUM_NUVIO_ENGINE_PREFIX", "")
    if not prefix or Path(prefix).resolve() != Path(engine_stamp).resolve().parent:
        raise ValueError("engine stamp path does not match configured Nuvio prefix")
    result = {
        "schema": 2, "sourceCommit": current["sourceCommit"],
        "sourceTreeOid": after["sourceTreeOid"],
        "sourceIndexMetadataSha256Before": before["sourceIndexMetadataSha256"],
        "sourceIndexMetadataSha256After": after["sourceIndexMetadataSha256"],
        "trackedIndexRecordCount": after["trackedIndexRecordCount"],
        "branchRef": branch_ref, "runId": run_id,
        "runKind": "local" if local_run else "github-actions",
        "sourceDirty": False, "trackedSourceClean": True,
        "apk": Path(apk).name, "apkBytes": len(apk_data),
        "apkSha256": apk_hash,
        "package": verified["package"], "versionCode": verified["versionCode"],
        "versionName": verified["versionName"], "contentEndpoint": endpoint,
        "build": cache,
        "engineStampSha256": hashlib.sha256(Path(engine_stamp).read_bytes()).hexdigest(),
        "unsignedVerification": verified,
    }
    Path(output).write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return result

def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser()
    sub = p.add_subparsers(dest="cmd", required=True)
    for name in ("config", "release-cmake-args"):
        x = sub.add_parser(name)
        x.add_argument("--abi", required=True)
        x.add_argument("--build-root", required=True)
        x.add_argument("--out-dir", required=True)
    sel = sub.add_parser("select-qt-apk")
    sel.add_argument("--build-dir", required=True)
    dispatch = sub.add_parser("validate-dispatch")
    dispatch.add_argument("--diagnostic", action="store_true")
    dispatch.add_argument("--release", action="store_true")
    dispatch.add_argument("--package", default=DEFAULT_PACKAGE)
    dispatch.add_argument("--version-code", default="2")
    dispatch.add_argument("--endpoint", default=DEFAULT_ENDPOINT)
    dispatch.add_argument("--channel", choices=("preview", "alpha"), default="preview")
    dispatch.add_argument("--version-name", default="")
    verify = sub.add_parser("verify-unsigned")
    for name in ("repo", "apk", "package", "aapt", "zipalign", "output"):
        verify.add_argument("--" + name, required=True)
    verify.add_argument("--version-code", type=int, required=True)
    verify.add_argument("--engine-library", required=True)
    verify.add_argument("--version-name", default="")
    prov = sub.add_parser("write-provenance")
    for name in ("repo", "build-dir", "apk", "verification", "engine-stamp", "endpoint", "output",
                 "source-before", "source-after", "branch-ref", "run-id"):
        prov.add_argument("--" + name, required=True)
    snapshot = sub.add_parser("source-snapshot")
    snapshot.add_argument("--repo", required=True)
    snapshot.add_argument("--output", required=True)
    compare = sub.add_parser("verify-source-snapshots")
    compare.add_argument("--repo", required=True)
    compare.add_argument("--before", required=True)
    compare.add_argument("--after-output", required=True)
    a = p.parse_args(argv)
    try:
        if a.cmd == "validate-dispatch":
            if a.diagnostic and a.release:
                raise ValueError("diagnostic and release-unsigned modes are mutually exclusive")
            if a.release:
                env = {"COLOSSEUM_ANDROID_APK_FLAVOUR": "release-unsigned",
                       "COLOSSEUM_NUVIO_ENGINE_PREFIX": "validated-after-engine-prep",
                       "COLOSSEUM_ANDROID_RELEASE_CHANNEL": a.channel,
                       "COLOSSEUM_ANDROID_PREVIEW_PACKAGE": a.package if a.channel == "preview" else "",
                       "COLOSSEUM_ANDROID_APPLICATION_ID": a.package if a.channel == "alpha" else "",
                       "COLOSSEUM_ANDROID_VERSION_NAME": a.version_name,
                       "COLOSSEUM_ANDROID_VERSION_CODE": a.version_code,
                       "COLOSSEUM_CONTENT_API_URL": a.endpoint}
                resolve_build_config("arm64-v8a", "build-root", "out-dir", env)
            return 0
        if a.cmd in ("config", "release-cmake-args"):
            cfg = resolve_build_config(a.abi, a.build_root, a.out_dir)
            if a.cmd == "config":
                print(cfg.build_dir, cfg.flavour, cfg.artifact_name, sep="\n")
            else:
                print("\n".join(release_cmake_args(cfg)))
            return 0
        if a.cmd == "select-qt-apk":
            root = Path(a.build_dir)
            candidates = (root / "android-build" / "colosseum.apk",
                          root / "android-build-colosseum" / "colosseum.apk")
            existing = [item for item in candidates if item.is_file()]
            if len(existing) != 1:
                raise ValueError(f"expected exactly one known Qt APK output, found {len(existing)}")
            print(existing[0])
            return 0
        if a.cmd == "verify-unsigned":
            result = run_unsigned_verification(a.apk, a.package, a.version_code, a.aapt,
                                              a.zipalign, a.engine_library, a.version_name or project_version(a.repo))
            Path(a.output).write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
            print(f"UNSIGNED_APK_STRUCTURAL_CHECK_OK sha256={result['apkSha256']}")
            return 0
        if a.cmd == "write-provenance":
            result = write_provenance(a.repo, a.build_dir, a.apk, a.verification,
                                      a.engine_stamp, a.endpoint, a.output, a.source_before,
                                      a.source_after, a.branch_ref, a.run_id)
            print(f"ANDROID_UNSIGNED_RELEASE_PROVENANCE_OK commit={result['sourceCommit']} sha256={result['apkSha256']}")
            return 0
        if a.cmd == "source-snapshot":
            result = write_source_snapshot(a.repo, a.output)
            print(f"ANDROID_SOURCE_SNAPSHOT_OK commit={result['sourceCommit']} index={result['sourceIndexMetadataSha256']}")
            return 0
        if a.cmd == "verify-source-snapshots":
            result = verify_source_snapshots(a.repo, a.before, a.after_output)
            print(f"ANDROID_SOURCE_SNAPSHOT_STABLE_OK commit={result['sourceCommit']} index={result['sourceIndexMetadataSha256']}")
            return 0
        raise ValueError("unknown command")
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as exc:
        print(f"release_apk.py: {exc}", file=sys.stderr)
        return 2

if __name__ == "__main__":
    raise SystemExit(main())
