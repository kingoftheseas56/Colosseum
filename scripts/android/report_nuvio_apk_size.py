#!/usr/bin/env python3
"""Measure the Android Nuvio increment with proof instrumentation disabled.

Both inputs must be built from the same commit/toolchain/signing configuration.
The only changed option is COLOSSEUM_NUVIO_ANDROID=OFF/ON. APK bytes include ZIP
compression and signing; native-library bytes are uncompressed ZIP members.
"""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile


def inspect_apk(path: Path, abi: str) -> dict:
    with zipfile.ZipFile(path) as archive:
        libraries = {entry.filename: entry.file_size for entry in archive.infolist()
                     if entry.filename.startswith(f"lib/{abi}/") and
                     entry.filename.endswith(".so")}
    if not libraries:
        raise ValueError(f"{path.name} has no {abi} native libraries")
    with path.open("rb") as source:
        digest = hashlib.file_digest(source, "sha256").hexdigest()
    return {"file": path.name, "bytes": path.stat().st_size,
            "sha256": digest,
            "native_uncompressed_bytes": sum(libraries.values()),
            "native_libraries": libraries}


def compare(baseline: Path, current: Path, abi: str) -> dict:
    before, after = inspect_apk(baseline, abi), inspect_apk(current, abi)
    return {"abi": abi, "baseline": before, "nuvio": after,
            "apk_delta_bytes": after["bytes"] - before["bytes"],
            "apk_delta_percent": round(100 * (after["bytes"] - before["bytes"]) /
                                       before["bytes"], 4),
            "native_delta_bytes": after["native_uncompressed_bytes"] -
                                  before["native_uncompressed_bytes"]}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", required=True, type=Path)
    parser.add_argument("--current", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--commit", required=True)
    args = parser.parse_args()
    rows = [compare(args.baseline / f"colosseum-{abi}-debug.apk",
                    args.current / f"colosseum-{abi}-debug.apk", abi)
            for abi in ("arm64-v8a", "x86_64")]
    args.output.mkdir(parents=True, exist_ok=True)
    report = {"commit": args.commit, "comparison": "same commit, Android Nuvio OFF versus ON",
              "proof_instrumentation": False, "build_type": "Debug", "architectures": rows}
    (args.output / "apk-size.json").write_text(json.dumps(report, indent=2) + "\n")
    lines = ["# Android Nuvio APK size", "", f"Commit: {args.commit}", "",
             "Same commit and toolchain, COLOSSEUM_NUVIO_ANDROID OFF versus ON.",
             "Debug APKs. Proof instrumentation is disabled for both measurements.", ""]
    for row in rows:
        lines.append(f"- {row['abi']}: {row['baseline']['bytes']:,} → "
                     f"{row['nuvio']['bytes']:,} bytes; "
                     f"delta {row['apk_delta_bytes']:+,} bytes "
                     f"({row['apk_delta_percent']:+.4f}%). "
                     f"Uncompressed native-library delta: {row['native_delta_bytes']:+,} bytes.")
    markdown = "\n".join(lines) + "\n"
    (args.output / "apk-size.md").write_text(markdown)
    print(markdown, end="")


if __name__ == "__main__":
    main()
