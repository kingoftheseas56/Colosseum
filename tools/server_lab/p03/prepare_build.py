#!/usr/bin/env python3
"""Generate the standalone P03 dependency input from Colosseum's live CMake authority."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

START = "add_library(colosseum_libtorrent INTERFACE)"
END = "add_library(colosseum_update_crypto INTERFACE)"


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--colosseum-cmake", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()

    source = args.colosseum_cmake.read_bytes()
    text = source.decode("utf-8")
    start = text.find(START)
    end = text.find(END)
    if start < 0 or end < 0 or end <= start:
        raise SystemExit("unable to locate the live colosseum_libtorrent authority block")

    fragment = text[start:end].rstrip() + "\n"
    args.out.mkdir(parents=True, exist_ok=True)
    cmake_path = args.out / "ColosseumLibtorrentAuthority.cmake"
    metadata_path = args.out / "authority.json"
    cmake_path.write_text(fragment, encoding="utf-8", newline="\n")
    metadata = {
        "schema_version": 1,
        "source": args.colosseum_cmake.as_posix(),
        "source_sha256": sha256(source),
        "fragment_sha256": sha256(fragment.encode("utf-8")),
        "start_anchor": START,
        "end_anchor": END,
        "generated_file": cmake_path.name,
    }
    metadata_path.write_text(json.dumps(metadata, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(cmake_path.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
