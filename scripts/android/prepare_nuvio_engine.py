#!/usr/bin/env python3
"""Prepare the hardened private Nuvio engine for the Colosseum Android backend.

Checksum/pin-aware standalone preparation with no global setup:

  1. verify the donor source worktree sits at the PINNED COMMIT (a mutable
     tag is never trusted on its own), that the hardening patch recorded in
     native/third_party/nuvio-engine is exactly the worktree's delta over
     that commit, and that the patch applies cleanly to a pristine extraction
     of the same commit (reproducibility proof, fail-closed on drift);
  2. configure and build libnuvio_engine.so for the requested Android ABI
     with the donor's own pinned dependency graph;
  3. verify the donor's twelve pinned libtorrent patches were actually
     applied in the populated dependency source (sentinel presence, not
     trust);
  4. install into <prefix>/android/<abi>/lib, write a content-rich
     provenance stamp, and re-run the fail-closed artifact verifier over the
     result. The stamp alone is never sufficient: the package gate validates
     its contents, so an unpatched release AAR can never pass.

  --verify-only runs step 1 (and the pristine-application proof) without
  building; the pristine proof also runs automatically before every build.

Usage:
  python scripts/android/prepare_nuvio_engine.py \
      --source <path-to-nuvio-engine-donor-worktree> \
      --ndk <path-to-android-ndk> \
      --abi arm64-v8a --api 28 \
      --prefix <install-prefix>
"""

import argparse
import hashlib
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
from datetime import datetime, timezone

PINNED_BRANCH = "codex/nuvio-android-hardening-20261018"
PINNED_TAG = "v0.1.5"
PINNED_SOURCE_COMMIT = "99a18601fa12858cc8c62576dc72b87edbd81b9a"
NUVIO_ENGINE_API_VERSION = 3
LIBTORRENT_PATCH_COUNT = 12
PATCH_SENTINELS = {
    # Source-level sentinels proving the hardening patch is applied in the
    # donor worktree (verified-only piece delivery + Android discovery off).
    "src/torrent/libtorrent_stream_bridge.cpp": [
        "Only whole-piece hash-verified bytes may reach the HTTP client",
    ],
    "src/torrent/libtorrent_backend.cpp": [
        "Android profile: local peer discovery and NAT port mapping stay off",
        "NUVIO_ENGINE_DISABLE_DISCOVERY",
    ],
}
# finished_run_end is the speculative-path helper the hardening REMOVES: it
# must be GONE. PRESENT and ABSENT lists must stay disjoint, so the
# contradiction that made every source tree unverifiable cannot recur.
REQUIRED_ABSENT = {
    "src/torrent/libtorrent_stream_bridge.cpp": ["finished_run_end"],
}
STAMP_NAME = ".colosseum-nuvio-hardened-stamp"


def die(message: str) -> "NoReturn":  # type: ignore[valid-type]
    print(f"prepare_nuvio_engine: {message}", file=sys.stderr)
    sys.exit(1)


def run(command, cwd=None, check=True):
    result = subprocess.run(
        command, cwd=cwd, capture_output=True, text=True, encoding="utf-8", errors="replace"
    )
    if check and result.returncode != 0:
        print(result.stdout, file=sys.stderr)
        print(result.stderr, file=sys.stderr)
        die(f"command failed ({result.returncode}): {' '.join(command)}")
    return result


def git(args, source: pathlib.Path, check=True):
    return run(["git", *args], cwd=str(source), check=check)


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def normalized(text: str) -> str:
    return text.replace("\r\n", "\n").replace("\r", "\n")


def sentinels_present(source: pathlib.Path) -> bool:
    for relative, sentinels in PATCH_SENTINELS.items():
        text = (source / relative).read_text(encoding="utf-8", errors="replace")
        for sentinel in sentinels:
            if sentinel not in text:
                return False
    for relative, sentinels in REQUIRED_ABSENT.items():
        text = (source / relative).read_text(encoding="utf-8", errors="replace")
        for sentinel in sentinels:
            if sentinel in text:
                return False
    return True


def check_sentinel_lists_consistent() -> None:
    for relative, present in PATCH_SENTINELS.items():
        absent = REQUIRED_ABSENT.get(relative, [])
        overlap = sorted(set(present) & set(absent))
        if overlap:
            die(
                f"contradictory sentinel configuration for {relative}: "
                f"{overlap} must be both present and absent"
            )


def prove_patch_applies_to_pristine_pin(source: pathlib.Path, patch: pathlib.Path) -> None:
    """Export the pinned commit and prove the vendored patch applies to it."""
    pristine = pathlib.Path(tempfile.mkdtemp(prefix="nuvio-pristine-"))
    try:
        archive_path = pristine / "pin.tar"
        with archive_path.open("wb") as handle:
            # Export the committed blobs byte for byte: the hardened working
            # donor may carry core.autocrlf=true, which would otherwise turn
            # this "pristine" extraction into CRLF working-tree bytes.
            exporter = subprocess.Popen(
                ["git", "-c", "core.autocrlf=false", "archive", "--format=tar",
                 PINNED_SOURCE_COMMIT],
                cwd=str(source),
                stdout=subprocess.PIPE,
            )
            shutil.copyfileobj(exporter.stdout, handle)
            if exporter.wait() != 0:
                die("git archive of the pinned commit failed")
        run(["tar", "-xf", str(archive_path), "-C", str(pristine)])
        archive_path.unlink(missing_ok=True)
        applied = git(["apply", "--check", "--binary", str(patch)], pristine, check=False)
        if applied.returncode != 0:
            print(applied.stdout, file=sys.stderr)
            print(applied.stderr, file=sys.stderr)
            die(
                "the vendored hardening patch does NOT apply to a pristine "
                f"extraction of {PINNED_SOURCE_COMMIT}; provenance is broken"
            )
        git(["apply", "--binary", str(patch)], pristine)
        # Not merely an apply --check: the resulting materialized source
        # bytes must equal the exact audited donor delta (new files included).
        changed = git(["diff", "--name-only", PINNED_SOURCE_COMMIT], source).stdout.splitlines()
        if not changed:
            die("no donor hardening delta exists")
        for relative in changed:
            relative = relative.strip()
            actual, expected = pristine / relative, source / relative
            if actual.exists() != expected.exists():
                die(f"patch materialization differs from donor: {relative}")
            if actual.exists() and normalized(actual.read_text(
                encoding="utf-8", errors="replace"
            )) != normalized(expected.read_text(encoding="utf-8", errors="replace")):
                die(f"applied patch differs from donor source: {relative}")
    finally:
        shutil.rmtree(pristine, ignore_errors=True)


def verify_source(source: pathlib.Path, patch: pathlib.Path) -> str:
    if not (source / "CMakeLists.txt").exists():
        die(f"{source} is not a nuvio_engine source tree")
    head = git(["rev-parse", "HEAD"], source).stdout.strip()
    if head != PINNED_SOURCE_COMMIT:
        die(
            f"HEAD {head} is not the pinned hardening base {PINNED_SOURCE_COMMIT}; "
            "the hardening patch must be the only delta over the pristine pin"
        )
    tag = git(["rev-parse", f"{PINNED_TAG}^{{commit}}"], source).stdout.strip()
    if tag != PINNED_SOURCE_COMMIT:
        die(
            f"{PINNED_TAG} resolves to {tag}, not {PINNED_SOURCE_COMMIT}: the "
            "tag moved and is not a pin; the exact commit is authoritative"
        )
    prove_patch_applies_to_pristine_pin(source, patch)

    # The dirty worktree delta must BE the vendored patch, byte for byte
    # (normalized line endings): no undocumented extra edits, no drift.
    diff = git(["diff", "--binary", PINNED_SOURCE_COMMIT], source).stdout
    patch_text = patch.read_text(encoding="utf-8", errors="replace")
    if normalized(diff) != normalized(patch_text):
        die(
            "the donor worktree delta over the pin does not match the vendored "
            "hardening patch; refusing to prepare an unproven artifact"
        )
    if not sentinels_present(source):
        die(
            "hardening sentinels not verifiable in the donor source; refusing "
            "to prepare an unhardened artifact"
        )
    # Every invocation, even --verify-only, proves the donor declares twelve
    # intact, LF-normalized pinned patch files (not just their string names).
    validate_libtorrent_patch_sources(source)
    return head


def parse_libtorrent_patch_table(source: pathlib.Path):
    """Read the (patch, file, sentinel) triples from the donor's own pin."""
    text = (
        source / "cmake" / "Dependencies.cmake"
    ).read_text(encoding="utf-8", errors="replace")

    def extract_list(name: str):
        match = re.search(rf"set\(\s*{name}\s*(.*?)\)", text, re.S)
        if match is None:
            die(f"cannot find set({name} ...) in the donor Dependencies.cmake")
        return re.findall(r'"([^"]+)"', match.group(1))

    patches = extract_list("nuvio_libtorrent_patches")
    files = extract_list("nuvio_libtorrent_patch_files")
    sentinels = extract_list("nuvio_libtorrent_patch_sentinels")
    if not (
        len(patches) == len(files) == len(sentinels) == LIBTORRENT_PATCH_COUNT
    ):
        die(
            f"expected {LIBTORRENT_PATCH_COUNT} pinned libtorrent patches, "
            f"found patches={len(patches)} files={len(files)} "
            f"sentinels={len(sentinels)}"
        )
    return list(zip(patches, files, sentinels))


LIBTORRENT_PIN = "740a0b9aeabe00e762cc0efe4a0f27593db2550b"


def portable_libtorrent_patch_bytes(path: pathlib.Path) -> bytes:
    """Match the donor's LF copy used by CMake, even after Windows checkout."""
    normalized_patch = path.read_bytes().replace(b"\r\n", b"\n")
    if b"\r" in normalized_patch:
        die(f"pinned libtorrent patch has invalid CR line endings: {path}")
    return normalized_patch


def validate_libtorrent_patch_sources(source: pathlib.Path) -> list:
    """Check the full pinned patch table, patch targets, and LF normalization."""
    table = parse_libtorrent_patch_table(source)
    for template, relative, sentinel in table:
        patch = pathlib.Path(
            template.replace("${CMAKE_CURRENT_FUNCTION_LIST_DIR}", str(source / "cmake"))
        )
        if not patch.is_file():
            die(f"pinned libtorrent patch absent: {patch}")
        # Git on Windows may check out canonical LF patches as CRLF. This
        # normalization changes no patch hunk semantics and mirrors CMake.
        content = portable_libtorrent_patch_bytes(patch)
        touched = [
            (a.decode("utf-8"), b.decode("utf-8"))
            for a, b in re.findall(rb"^diff --git a/(\S+) b/(\S+)", content, re.M)
        ]
        if not touched or any(left != right for left, right in touched):
            die(f"pinned libtorrent patch has an invalid target: {patch}")
        if relative not in [left for left, _ in touched] or not sentinel:
            die(f"pinned libtorrent patch table mismatches its target: {patch}")
    return table


def verify_libtorrent_patches_applied(source: pathlib.Path, build_dir: pathlib.Path) -> None:
    """Prove all hunks (not just textual sentinels) exist in the pinned tree."""
    table = validate_libtorrent_patch_sources(source)
    populated = build_dir / "_deps" / "nuvio_libtorrent-src"
    if not populated.is_dir():
        die(f"populated libtorrent source missing after configure: {populated}")
    head = git(["rev-parse", "HEAD"], populated, check=False)
    if head.returncode != 0 or head.stdout.strip() != LIBTORRENT_PIN:
        die(f"populated libtorrent is not the pinned {LIBTORRENT_PIN}")
    missing = []
    for patch_path, file_relative, sentinel in table:
        patch = pathlib.Path(
            patch_path.replace("${CMAKE_CURRENT_FUNCTION_LIST_DIR}", str(source / "cmake"))
        )
        target = populated / file_relative
        if not target.exists():
            missing.append(f"{patch.name}: {file_relative} missing")
            continue
        if sentinel not in target.read_text(encoding="utf-8", errors="replace"):
            missing.append(f"{patch.name}: sentinel {sentinel!r} absent")
        reverse = git(
            ["apply", "--unidiff-zero", "--reverse", "--check", str(patch)],
            populated, check=False,
        )
        if reverse.returncode != 0:
            missing.append(f"{patch.name}: full reverse-check failed")
    if missing:
        for line in missing:
            print(f"prepare_nuvio_engine: unapplied libtorrent patch: {line}",
                  file=sys.stderr)
        die("the pinned libtorrent patches were not fully applied by configure")


def main() -> int:
    check_sentinel_lists_consistent()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=pathlib.Path)
    parser.add_argument("--ndk", type=pathlib.Path)
    parser.add_argument("--abi", default="arm64-v8a")
    parser.add_argument("--api", default="28")
    parser.add_argument("--prefix", type=pathlib.Path)
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--ninja", default="ninja")
    parser.add_argument(
        "--patch",
        type=pathlib.Path,
        default=pathlib.Path(__file__).resolve().parents[2]
        / "native"
        / "third_party"
        / "nuvio-engine"
        / "patches"
        / "0001-android-hardening.patch",
    )
    parser.add_argument(
        "--verify-only",
        action="store_true",
        help="run the source/provenance verification without building",
    )
    args = parser.parse_args()

    source = args.source.resolve()
    patch = args.patch.resolve()
    if not patch.exists():
        die(f"hardening patch missing: {patch}")
    head = verify_source(source, patch)
    if args.verify_only:
        print(
            f"verified donor source at pinned commit {head}; the vendored "
            "patch applies to a pristine extraction of that commit"
        )
        return 0

    if args.ndk is None or args.prefix is None:
        die("--ndk and --prefix are required unless --verify-only is used")
    ndk = args.ndk.resolve()
    prefix = args.prefix.resolve()
    toolchain = ndk / "build" / "cmake" / "android.toolchain.cmake"
    if not toolchain.exists():
        die(f"NDK toolchain file missing: {toolchain}")

    build_dir = source / "build-android" / args.abi
    configure = [
        args.cmake,
        "-S", str(source),
        "-B", str(build_dir),
        "-G", "Ninja",
        f"-DCMAKE_TOOLCHAIN_FILE={toolchain}",
        f"-DANDROID_ABI={args.abi}",
        f"-DANDROID_PLATFORM=android-{args.api}",
        "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_SHARED_LINKER_FLAGS=-Wl,-z,max-page-size=16384",
        "-DNUVIO_ENGINE_BUILD_SHARED=ON",
        "-DNUVIO_ENGINE_ENABLE_LIBTORRENT=ON",
        "-DNUVIO_ENGINE_BUILD_TESTS=OFF",
    ]
    run(configure)
    # The donor's configure applies the twelve pinned libtorrent patches to
    # the populated dependency source; prove it instead of trusting it.
    verify_libtorrent_patches_applied(source, build_dir)
    run([args.cmake, "--build", str(build_dir), "--target", "nuvio_engine", "--parallel", "4"])

    library = build_dir / "libnuvio_engine.so"
    if not library.exists():
        die(f"expected library missing after build: {library}")

    install_lib_dir = prefix / "android" / args.abi / "lib"
    install_lib_dir.mkdir(parents=True, exist_ok=True)
    installed = install_lib_dir / "libnuvio_engine.so"
    shutil.copyfile(library, installed)

    stamp = {
        "kind": "colosseum-nuvio-hardened",
        "source_commit": head,
        "source_branch": PINNED_BRANCH,
        "source_tag": PINNED_TAG,
        "abi": args.abi,
        "android_api": int(args.api),
        "api_version": NUVIO_ENGINE_API_VERSION,
        "libtorrent_patch_count": LIBTORRENT_PATCH_COUNT,
        "library": str(installed),
        "build_artifact": str(library.resolve()),
        "source_worktree": str(source),
        "library_sha256": sha256(installed),
        "patch": str(patch),
        "patch_sha256": sha256(patch),
        "created_utc": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
    }
    stamp_path = prefix / STAMP_NAME
    stamp_path.write_text(json.dumps(stamp, indent=2) + "\n", encoding="utf-8")
    print(f"prepared {installed}")
    print(f"stamp {stamp_path}")

    verifier = pathlib.Path(__file__).resolve().parent / "verify_nuvio_hardened_artifact.py"
    run([sys.executable, str(verifier), "--prefix", str(prefix), "--abi", args.abi,
         "--patch", str(patch), "--source", str(source)])
    return 0


if __name__ == "__main__":
    sys.exit(main())
