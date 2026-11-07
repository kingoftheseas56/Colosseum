#!/usr/bin/env python3
"""Fail-closed validation of a prepared hardened Nuvio engine artifact.

The Colosseum package gate calls this instead of checking stamp existence:
an empty, forged, stale, wrong-ABI, or unpatched-binary artifact must fail.
It validates the CONTENTS of the provenance stamp and the artifact it
describes:

  * stamp parses and declares the expected kind, pinned source commit/tag,
    API version, and the requested ABI;
  * the recorded library is the canonical prefix location and its SHA-256
    matches the actual file (stale or swapped binaries fail);
  * the recorded patch identity matches the vendored hardening patch shipped
    in this repository (artifacts built from anything else fail);
  * the library is a 64-bit little-endian ELF shared object for the expected
    machine (an unpatched release AAR/JAR is a zip and fails here).

Usage:
  python scripts/android/verify_nuvio_hardened_artifact.py \
      --prefix <install-prefix> [--abi arm64-v8a] \
      [--patch <vendored 0001-android-hardening.patch>]

Exit 0 = artifact provenance is proven; exit 1 = rejected (reason on stderr).
"""

import argparse
import hashlib
import json
import pathlib
import struct
import sys

STAMP_NAME = ".colosseum-nuvio-hardened-stamp"
EXPECTED_KIND = "colosseum-nuvio-hardened"
PINNED_SOURCE_COMMIT = "99a18601fa12858cc8c62576dc72b87edbd81b9a"
PINNED_TAG = "v0.1.5"
PINNED_API_VERSION = 3
PINNED_LIBTORRENT_PATCH_COUNT = 12

ELF_MAGIC = b"\x7fELF"
ELFCLASS64 = 2
ELFDATA2LSB = 1
ET_DYN = 3
ELF_MACHINE_FOR_ABI = {
    "arm64-v8a": 0xB7,  # EM_AARCH64
    "armeabi-v7a": 0x28,  # EM_ARM
    "x86_64": 0x3E,  # EM_X86_64
    "x86": 0x03,  # EM_386
}


def die(message: str) -> "NoReturn":  # type: ignore[valid-type]
    print(f"verify_nuvio_hardened_artifact: {message}", file=sys.stderr)
    sys.exit(1)


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def elf_matches_abi(data: bytes, abi: str) -> bool:
    machine = ELF_MACHINE_FOR_ABI.get(abi)
    if machine is None or len(data) < 20 or data[:4] != ELF_MAGIC:
        return False
    if data[4] != ELFCLASS64 or data[5] != ELFDATA2LSB:
        return False
    e_type = struct.unpack_from("<H", data, 16)[0]
    e_machine = struct.unpack_from("<H", data, 18)[0]
    return e_type == ET_DYN and e_machine == machine


# Dynamic export and 16 KiB PT_LOAD alignment checks are independent of the
# self-asserted provenance stamp; a header-only fake ELF cannot pass.
REQUIRED_API_EXPORTS = {
    "nuvio_engine_api_version", "nuvio_engine_create",
    "nuvio_engine_destroy", "nuvio_engine_add_torrent",
    "nuvio_engine_poll_event", "nuvio_engine_prepare_stream",
    "nuvio_engine_stop_stream", "nuvio_engine_get_stats",
    "nuvio_engine_get_stream_stats",
}


def elf_dynamic_exports_and_alignment(data: bytes) -> tuple[set[str], bool]:
    if len(data) < 64:
        return set(), False
    shoff = struct.unpack_from("<Q", data, 40)[0]
    phoff = struct.unpack_from("<Q", data, 32)[0]
    phentsize, phnum = struct.unpack_from("<HH", data, 54)
    shentsize, shnum = struct.unpack_from("<HH", data, 58)
    aligned = False
    loads = 0
    for index in range(phnum):
        off = phoff + index * phentsize
        if off + 56 > len(data) or phentsize < 56:
            return set(), False
        ptype = struct.unpack_from("<I", data, off)[0]
        if ptype != 1:
            continue
        loads += 1
        poffset, vaddr = struct.unpack_from("<QQ", data, off + 8)
        align = struct.unpack_from("<Q", data, off + 48)[0]
        if align < 16384 or (poffset - vaddr) % 16384:
            return set(), False
        aligned = True
    if not aligned or not loads or not shnum or shentsize < 64:
        return set(), False
    def section(i: int):
        off = shoff + i * shentsize
        if i < 0 or i >= shnum or off + 64 > len(data):
            return None
        return struct.unpack_from("<IIQQQQIIQQ", data, off)
    exports = set()
    for index in range(shnum):
        header = section(index)
        if not header or header[1] != 11:  # SHT_DYNSYM
            continue
        linked = section(header[6])
        if not linked or linked[1] != 3:  # SHT_STRTAB
            continue
        strings = data[linked[4]:linked[4] + linked[5]]
        start, size, stride = header[4], header[5], header[9]
        if stride < 24 or start + size > len(data):
            continue
        for symbol_off in range(start, start + size, stride):
            if symbol_off + 24 > len(data):
                break
            name_off = struct.unpack_from("<I", data, symbol_off)[0]
            shndx = struct.unpack_from("<H", data, symbol_off + 6)[0]
            if shndx == 0 or name_off >= len(strings):
                continue
            name = strings[name_off:].split(b"\0", 1)[0].decode("ascii", errors="ignore")
            exports.add(name)
    return exports, aligned


def verify(prefix: pathlib.Path, abi: str, patch: pathlib.Path, source_arg=None) -> dict:
    if abi not in ELF_MACHINE_FOR_ABI:
        die(f"unknown abi {abi!r}")
    stamp_path = prefix / STAMP_NAME
    if not stamp_path.exists():
        die(f"hardened stamp missing: {stamp_path}")
    try:
        stamp_text = stamp_path.read_text(encoding="utf-8")
    except OSError as error:
        die(f"hardened stamp is unreadable: {error}")
    if not stamp_text.strip():
        die("hardened stamp is empty")
    try:
        stamp = json.loads(stamp_text)
    except json.JSONDecodeError as error:
        die(f"hardened stamp is not valid JSON: {error}")
    if not isinstance(stamp, dict):
        die("hardened stamp is not a JSON object")

    if stamp.get("kind") != EXPECTED_KIND:
        die(f"stamp kind {stamp.get('kind')!r} is not {EXPECTED_KIND!r}")
    if stamp.get("source_commit") != PINNED_SOURCE_COMMIT:
        die(
            "stamp source commit "
            f"{stamp.get('source_commit')!r} is not the pinned hardening base "
            f"{PINNED_SOURCE_COMMIT}: the artifact provenance is stale or forged"
        )
    if stamp.get("source_tag") != PINNED_TAG:
        die(f"stamp source tag {stamp.get('source_tag')!r} is not {PINNED_TAG!r}")
    if stamp.get("api_version") != PINNED_API_VERSION:
        die(
            f"stamp api version {stamp.get('api_version')!r} is not the pinned "
            f"C API version {PINNED_API_VERSION}"
        )
    if stamp.get("libtorrent_patch_count") != PINNED_LIBTORRENT_PATCH_COUNT:
        die(
            "stamp does not attest the "
            f"{PINNED_LIBTORRENT_PATCH_COUNT} pinned libtorrent patches "
            f"(recorded {stamp.get('libtorrent_patch_count')!r})"
        )
    if stamp.get("abi") != abi:
        die(f"stamp abi {stamp.get('abi')!r} does not match the requested {abi!r}")

    library = pathlib.Path(str(stamp.get("library", "")))
    canonical = prefix / "android" / abi / "lib" / "libnuvio_engine.so"
    if library != canonical:
        die(
            f"stamp library {library} is not the canonical artifact location {canonical}"
        )
    if not library.exists():
        die(f"hardened library missing: {library}")
    actual_library_sha = sha256_file(library)
    if stamp.get("library_sha256") != actual_library_sha:
        die(
            "library does not match its recorded identity: stale or forged "
            f"artifact (stamp {stamp.get('library_sha256')!r}, actual "
            f"{actual_library_sha!r})"
        )
    if not patch.exists():
        die(f"vendored hardening patch missing: {patch}")
    if stamp.get("patch_sha256") != sha256_file(patch):
        die(
            "artifact was not built from the vendored hardening patch "
            f"({patch}): patch identity mismatch"
        )
    library_bytes = library.read_bytes()
    if not elf_matches_abi(library_bytes, abi):
        die(
            f"library is not a 64-bit {abi} ELF shared object: an unpatched "
            "release AAR/JAR or wrong-ABI binary never qualifies as hardened"
        )
    exports, aligned = elf_dynamic_exports_and_alignment(library_bytes)
    if not aligned:
        die("library lacks valid 16 KiB-aligned ELF load segments")
    if not REQUIRED_API_EXPORTS.issubset(exports):
        die("library is missing genuine dynamic C API exports: " +
            ", ".join(sorted(REQUIRED_API_EXPORTS - exports)))

    # Fail closed when the donor source/build tree cannot be examined. A stamp
    # and plausible ELF are never sufficient evidence of the pinned hardening.
    source_value = stamp.get("source_worktree")
    artifact_value = stamp.get("build_artifact")
    if not isinstance(source_value, str) or not isinstance(artifact_value, str):
        die("missing source worktree and build artifact provenance")
    source = pathlib.Path(source_value).resolve()
    artifact = pathlib.Path(artifact_value).resolve()
    if source_arg is not None and source != source_arg.resolve():
        die("source worktree differs from the package gate's supplied donor")
    expected_artifact = source / "build-android" / abi / "libnuvio_engine.so"
    if artifact != expected_artifact or not artifact.exists():
        die("build artifact is not the pinned donor's expected output")
    if sha256_file(artifact) != actual_library_sha:
        die("packaged engine does not match the donor's build artifact")
    import importlib.util
    prepare = pathlib.Path(__file__).resolve().with_name("prepare_nuvio_engine.py")
    spec = importlib.util.spec_from_file_location("prepare_nuvio_engine", prepare)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.check_sentinel_lists_consistent()
    module.verify_source(source, patch)
    return stamp


def main() -> int:
    default_patch = (
        pathlib.Path(__file__).resolve().parents[2]
        / "native"
        / "third_party"
        / "nuvio-engine"
        / "patches"
        / "0001-android-hardening.patch"
    )
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", required=True, type=pathlib.Path)
    parser.add_argument("--abi", default="arm64-v8a")
    parser.add_argument("--patch", type=pathlib.Path, default=default_patch)
    parser.add_argument("--source", type=pathlib.Path)
    args = parser.parse_args()

    stamp = verify(args.prefix.resolve(), args.abi, args.patch.resolve(), args.source)
    print(
        "verified hardened nuvio engine: "
        f"abi={stamp['abi']} source={stamp['source_commit'][:12]} "
        f"library={stamp['library']}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
