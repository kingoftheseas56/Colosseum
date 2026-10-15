#!/usr/bin/env python3
"""
Colosseum code encyclopedia - generated source index.

Harvests each source file's OWN top-of-file comment into one index. It never
paraphrases and never invents: the source is the single source of truth, and this
tool only collects what the code already says about itself.

Two ideas carry the whole design:

  1. ACCEPTED vs CURRENT. The index publishes the comment a human last ACCEPTED,
     together with the blob hash it was accepted at. If the file changes, the entry
     is flagged DRIFTED and keeps showing the accepted text - the tool will not
     silently republish unreviewed prose. `--accept` is the only way forward.
  2. Gaps are reported, not hidden. A file with no file-level comment is listed
     UNDOCUMENTED so the backlog is countable instead of invisible.

Design: docs/superpowers/specs/2026-08-07-colosseum-code-encyclopedia.md
Lineage: Preflight-Architect issue #3 reference candidates r1 (structure, acceptance
state, integrity) and r2 (preamble grammar), executed and verified by Agent 0 before
adoption; the two revisions are inlined here as one script.

Usage:
  python scripts/code_encyclopedia.py --paths <manifest> --output <index.md> --state <state.json>
  python scripts/code_encyclopedia.py ... --check              # CI/pre-commit gate
  python scripts/code_encyclopedia.py ... --accept <path>      # ratify one file's new comment
  python scripts/code_encyclopedia.py ... --accept-all-drifted # ratify every drifted comment
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path, PurePosixPath

SCHEMA = 1
SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".qml", ".js"}

CPP_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx"}
CPP_PRAGMA_ONCE = re.compile(r"^#\s*pragma\s+once\s*$")
CPP_INCLUDE = re.compile(r"^#\s*include\b")
CPP_IFNDEF = re.compile(r"^#\s*ifndef\s+([A-Za-z_][A-Za-z0-9_]*)\s*$")
CPP_DEFINE = re.compile(r"^#\s*define\s+([A-Za-z_][A-Za-z0-9_]*)\s*$")

UNDOCUMENTED_NOTE = "_No explanatory comment was harvested after the allowed file preamble._"


class EncyclopediaError(RuntimeError):
    pass


@dataclass(frozen=True)
class Harvest:
    path: str
    blob: str
    comment: str | None


@dataclass(frozen=True)
class Accepted:
    blob: str
    comment: str | None


def repo_root() -> Path:
    try:
        result = subprocess.run(
            ["git", "rev-parse", "--show-toplevel"],
            check=True, capture_output=True, text=True
        )
    except (OSError, subprocess.CalledProcessError) as exc:
        raise EncyclopediaError("run inside a Git worktree with Git available") from exc
    return Path(result.stdout.strip()).resolve()


def normalize_path(raw: str) -> str:
    text = raw.strip().replace("\\", "/")
    path = PurePosixPath(text)
    if not text or path.is_absolute() or ".." in path.parts:
        raise EncyclopediaError(f"invalid manifest path: {raw!r}")
    normalized = path.as_posix()
    if Path(normalized).suffix.lower() not in SUFFIXES:
        raise EncyclopediaError(f"unsupported source suffix: {normalized}")
    return normalized


def read_manifest(path: Path, root: Path) -> list[str]:
    if not path.is_file():
        raise EncyclopediaError(f"missing manifest: {path}")
    seen: set[str] = set()
    items: list[str] = []
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        stripped = raw.strip()
        if not stripped or stripped.startswith("#"):
            continue
        normalized = normalize_path(stripped)
        if normalized in seen:
            raise EncyclopediaError(f"duplicate manifest path at line {number}: {normalized}")
        source = (root / normalized).resolve()
        try:
            source.relative_to(root)
        except ValueError as exc:
            raise EncyclopediaError(f"path escapes repository: {normalized}") from exc
        if not source.is_file():
            raise EncyclopediaError(f"missing source: {normalized}")
        seen.add(normalized)
        items.append(normalized)
    if not items:
        raise EncyclopediaError("manifest contains no source files")
    return sorted(items)


def git_blob(root: Path, rel: str, data: bytes) -> str:
    try:
        result = subprocess.run(
            ["git", "hash-object", f"--path={rel}", "--stdin"],
            cwd=root, input=data, check=True, capture_output=True
        )
    except (OSError, subprocess.CalledProcessError) as exc:
        raise EncyclopediaError(f"cannot hash source: {rel}") from exc
    return result.stdout.decode("ascii").strip()


# ── preamble grammar ─────────────────────────────────────────────────────────
# A file's explanation may sit below a bounded, contiguous declaration preamble
# (#pragma once, a MATCHED include guard, includes; QML imports/pragmas). Anything
# past that preamble that is not a comment means the file has no file-level
# explanation - an internal comment inside a namespace or function must never be
# harvested and published as the file's description.

def skip_blanks(lines: list[str], index: int) -> int:
    while index < len(lines) and not lines[index].strip():
        index += 1
    return index


def skip_cpp_preamble(lines: list[str], index: int) -> int:
    guard_skipped = False
    while True:
        index = skip_blanks(lines, index)
        if index >= len(lines):
            return index
        stripped = lines[index].strip()

        if CPP_PRAGMA_ONCE.fullmatch(stripped):
            index += 1
            continue

        if not guard_skipped:
            guard = CPP_IFNDEF.fullmatch(stripped)
            if guard:
                define_index = skip_blanks(lines, index + 1)
                if define_index < len(lines):
                    define = CPP_DEFINE.fullmatch(lines[define_index].strip())
                    if define and define.group(1) == guard.group(1):
                        guard_skipped = True
                        index = define_index + 1
                        continue
                # An incomplete or mismatched guard is a hard stop.
                return index

        if CPP_INCLUDE.match(stripped):
            index += 1
            continue

        return index


def skip_qml_preamble(lines: list[str], index: int) -> int:
    while True:
        index = skip_blanks(lines, index)
        if index >= len(lines):
            return index
        stripped = lines[index].strip()
        if stripped.startswith("import ") or stripped.startswith("pragma "):
            index += 1
            continue
        return index


def skip_js_preamble(lines: list[str], index: int) -> int:
    index = skip_blanks(lines, index)
    if index < len(lines) and lines[index].strip() == ".pragma library":
        index += 1
    return skip_blanks(lines, index)


def first_comment_index(rel: str, lines: list[str]) -> int:
    suffix = Path(rel).suffix.lower()
    index = skip_blanks(lines, 0)
    if suffix in CPP_SUFFIXES:
        return skip_cpp_preamble(lines, index)
    if suffix == ".qml":
        return skip_qml_preamble(lines, index)
    if suffix == ".js":
        return skip_js_preamble(lines, index)
    return index


def extract_comment(rel: str, data: bytes) -> str | None:
    try:
        text = data.decode("utf-8-sig")
    except UnicodeDecodeError as exc:
        raise EncyclopediaError(f"source is not UTF-8: {rel}") from exc

    # Normalize line endings before harvesting. Colosseum checks out CRLF on Windows,
    # and a harvested CRLF comment is written raw but read back through universal
    # newlines - so the index never compares equal to itself, it is rewritten on every
    # run, and --check is permanently red. The blob hash is already normalized by
    # `git hash-object --path` (gitattributes clean filter); the comment must match.
    text = text.replace("\r\n", "\n").replace("\r", "\n")

    lines = text.splitlines(keepends=True)
    index = first_comment_index(rel, lines)
    if index >= len(lines):
        return None

    first = lines[index].lstrip()

    if first.startswith("//"):
        start = index
        last_comment = index
        while index < len(lines):
            stripped = lines[index].lstrip()
            if stripped.startswith("//"):
                last_comment = index
                index += 1
                continue
            if not lines[index].strip():
                index += 1
                continue
            break
        return "".join(lines[start:last_comment + 1]).rstrip("\r\n")

    if first.startswith("/*"):
        start = index
        while index < len(lines):
            end = lines[index].find("*/")
            if end >= 0:
                return ("".join(lines[start:index]) + lines[index][:end + 2]).rstrip("\r\n")
            index += 1
        raise EncyclopediaError(f"unterminated top block comment: {rel}")

    return None


def harvest(root: Path, rel: str) -> Harvest:
    source = (root / rel).resolve()
    data = source.read_bytes()
    return Harvest(rel, git_blob(root, rel, data), extract_comment(rel, data))


# ── acceptance state ─────────────────────────────────────────────────────────

def state_payload(entries: dict[str, Accepted]) -> dict:
    return {
        "schema": SCHEMA,
        "entries": {
            path: {"accepted_blob": item.blob, "accepted_comment": item.comment}
            for path, item in sorted(entries.items())
        },
    }


def digest(payload: dict) -> str:
    raw = json.dumps(payload, sort_keys=True, separators=(",", ":"), ensure_ascii=False)
    return "sha256:" + hashlib.sha256(raw.encode("utf-8")).hexdigest()


def encode_state(entries: dict[str, Accepted]) -> str:
    payload = state_payload(entries)
    full = {**payload, "integrity": digest(payload)}
    return json.dumps(full, indent=2, sort_keys=True, ensure_ascii=False) + "\n"


def load_state(path: Path) -> dict[str, Accepted]:
    if not path.exists():
        return {}
    try:
        raw = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise EncyclopediaError(f"cannot read state: {path}") from exc
    if not isinstance(raw, dict):
        raise EncyclopediaError("state root must be an object")
    integrity = raw.pop("integrity", None)
    if integrity != digest(raw):
        raise EncyclopediaError("state integrity mismatch; do not hand-edit generated state")
    if raw.get("schema") != SCHEMA or not isinstance(raw.get("entries"), dict):
        raise EncyclopediaError("unsupported or malformed state")
    result: dict[str, Accepted] = {}
    for rel, value in raw["entries"].items():
        if not isinstance(rel, str) or not isinstance(value, dict):
            raise EncyclopediaError("malformed state entry")
        blob = value.get("accepted_blob")
        comment = value.get("accepted_comment")
        if not isinstance(blob, str) or (comment is not None and not isinstance(comment, str)):
            raise EncyclopediaError(f"malformed state entry: {rel}")
        result[rel] = Accepted(blob, comment)
    return result


# ── rendering ────────────────────────────────────────────────────────────────

def anchor(rel: str) -> str:
    token = "".join(c.lower() if c.isalnum() else "-" for c in rel)
    while "--" in token:
        token = token.replace("--", "-")
    return "file-" + token.strip("-")


def source_link(output: Path, root: Path, rel: str) -> str:
    return Path(os.path.relpath(root / rel, output.parent)).as_posix()


def render(root: Path, output: Path, state_path: Path,
           current: dict[str, Harvest], accepted: dict[str, Accepted]) -> str:
    total = len(current)
    undoc = sum(item.comment is None for item in accepted.values())
    drift = sum(accepted[p].blob != current[p].blob for p in current)
    lines = [
        "# Colosseum Code Encyclopedia -- Generated Source Index", "",
        "> **GENERATED FILE -- DO NOT EDIT.** Edit source comments, then run the generator.",
        f"> Acceptance state: `{Path(os.path.relpath(state_path, root)).as_posix()}`", "",
        "## Summary", "",
        f"- Total files: **{total}**",
        f"- Documented: **{total - undoc}**",
        f"- Undocumented: **{undoc}**",
        f"- Drifted: **{drift}**", "",
    ]
    for rel in sorted(current):
        now, old = current[rel], accepted[rel]
        flags: list[str] = []
        if now.blob != old.blob:
            flags.append("DRIFTED")
        if old.comment is None:
            flags.append("UNDOCUMENTED")
        if not flags:
            flags.append("CURRENT")
        lines += [
            f'<a id="{anchor(rel)}"></a>',
            f"## `{rel}`", "",
            f"- Status: **{' + '.join(flags)}**",
            f"- Accepted blob: `{old.blob}`",
            f"- Current blob: `{now.blob}`",
            f"- Source: [`{rel}`]({source_link(output, root, rel)})",
        ]
        if now.blob != old.blob:
            lines.append(
                "- Interpretation: the accepted description predates the current blob; "
                "read the source before relying on it."
            )
        lines.append("")
        if old.comment is None:
            lines += [UNDOCUMENTED_NOTE, ""]
        else:
            lines += ["```text", old.comment, "```", ""]
    return "\n".join(lines).rstrip() + "\n"


def atomic_write(path: Path, content: str) -> bool:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and path.read_text(encoding="utf-8") == content:
        return False
    with tempfile.NamedTemporaryFile(
        "w", encoding="utf-8", newline="", dir=path.parent,
        prefix=f".{path.name}.", suffix=".tmp", delete=False
    ) as handle:
        handle.write(content)
        temp_name = handle.name
    os.replace(temp_name, path)
    return True


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description="Generate the Colosseum source-comment index.")
    result.add_argument("--paths", required=True, type=Path)
    result.add_argument("--output", required=True, type=Path)
    result.add_argument("--state", required=True, type=Path)
    result.add_argument("--accept", action="append", default=[], metavar="PATH")
    result.add_argument("--accept-all-drifted", action="store_true")
    result.add_argument("--check", action="store_true")
    return result


def main(argv: list[str] | None = None) -> int:
    args = parser().parse_args(argv)
    try:
        root = repo_root()
        manifest = args.paths if args.paths.is_absolute() else root / args.paths
        output = args.output if args.output.is_absolute() else root / args.output
        state_path = args.state if args.state.is_absolute() else root / args.state
        if args.check and (args.accept or args.accept_all_drifted):
            raise EncyclopediaError("--check cannot be combined with acceptance flags")

        paths = read_manifest(manifest, root)
        current = {rel: harvest(root, rel) for rel in paths}
        state_exists = state_path.exists()
        previous = load_state(state_path)
        accepted = {rel: item for rel, item in previous.items() if rel in current}

        requested = {normalize_path(item) for item in args.accept}
        unknown = requested.difference(paths)
        if unknown:
            raise EncyclopediaError("--accept path not in manifest: " + ", ".join(sorted(unknown)))

        for rel in paths:
            if rel not in accepted:
                if args.check:
                    raise EncyclopediaError(f"state missing manifest entry: {rel}")
                accepted[rel] = Accepted(current[rel].blob, current[rel].comment)

        drifted = {rel for rel in paths if accepted[rel].blob != current[rel].blob}
        if args.accept_all_drifted:
            requested.update(drifted)
        for rel in requested:
            accepted[rel] = Accepted(current[rel].blob, current[rel].comment)

        markdown = render(root, output, state_path, current, accepted)
        state_text = encode_state(accepted)
        remaining = [rel for rel in paths if accepted[rel].blob != current[rel].blob]

        if args.check:
            output_ok = output.exists() and output.read_text(encoding="utf-8") == markdown
            state_ok = state_exists and state_path.read_text(encoding="utf-8") == state_text
            if not output_ok or not state_ok or remaining:
                if not output_ok:
                    print("CHECK FAILED: generated Markdown differs", file=sys.stderr)
                if not state_ok:
                    print("CHECK FAILED: acceptance state differs", file=sys.stderr)
                for rel in remaining:
                    print(f"CHECK FAILED: DRIFTED {rel}", file=sys.stderr)
                return 1
            print("CHECK OK")
            return 0

        wrote_state = atomic_write(state_path, state_text)
        wrote_output = atomic_write(output, markdown)
        undocumented = sum(accepted[p].comment is None for p in paths)
        print(
            f"entries={len(paths)} drifted={len(remaining)} undocumented={undocumented} "
            f"output={'updated' if wrote_output else 'unchanged'} "
            f"state={'updated' if wrote_state else 'unchanged'}"
        )
        return 0
    except EncyclopediaError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
