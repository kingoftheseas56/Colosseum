#!/usr/bin/env python3
"""Create the P01A probe project from Colosseum's live libtorrent authority."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

START = "add_library(colosseum_libtorrent INTERFACE)"
END = "add_library(colosseum_update_crypto INTERFACE)"


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def cmake_quote(path: Path) -> str:
    return path.resolve().as_posix().replace('"', '\\"')


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--colosseum-cmake", required=True, type=Path)
    parser.add_argument("--probe-source", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()

    source_bytes = args.colosseum_cmake.read_bytes()
    source = source_bytes.decode("utf-8")
    start = source.find(START)
    end = source.find(END)
    if start < 0 or end < 0 or end <= start:
        raise SystemExit("P01A_FAIL unable to locate unique colosseum_libtorrent authority block")
    if source.find(START, start + len(START)) >= 0:
        raise SystemExit("P01A_FAIL multiple colosseum_libtorrent authority blocks found")

    block = source[start:end].rstrip() + "\n"
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)

    project = f'''cmake_minimum_required(VERSION 3.16)\nproject(server1_p01a_probe LANGUAGES CXX)\nset(CMAKE_CXX_STANDARD 17)\nset(CMAKE_CXX_STANDARD_REQUIRED ON)\nfind_package(Qt6 REQUIRED COMPONENTS Core)\n\n# Extracted verbatim from native/CMakeLists.txt at workflow checkout time.\n{block}\nadd_executable(server1_p01a_probe "{cmake_quote(args.probe_source)}")\ntarget_link_libraries(server1_p01a_probe PRIVATE colosseum_libtorrent Qt6::Core)\n'''
    (out / "CMakeLists.txt").write_text(project, encoding="utf-8", newline="\n")

    authority = {
        "schema_version": 1,
        "source": str(args.colosseum_cmake.as_posix()),
        "source_sha256": sha256(source_bytes),
        "start_marker": START,
        "end_marker": END,
        "authority_block_sha256": sha256(block.encode("utf-8")),
        "authority_block_bytes": len(block.encode("utf-8")),
        "cxx_standard": 17,
        "probe_source": str(args.probe_source.as_posix()),
        "probe_source_sha256": sha256(args.probe_source.read_bytes()),
    }
    (out / "authority.json").write_text(
        json.dumps(authority, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(json.dumps(authority, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
