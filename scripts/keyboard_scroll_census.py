"""Generate the Arc 41 scroll-surface ownership census."""

from __future__ import annotations

import argparse
import json
import re
from collections import Counter
from pathlib import Path


SCROLL_TYPES = ("Flickable", "ListView", "GridView", "ScrollView", "SwipeView", "PathView")
KEYBOARD_OWNERS = (
    "KeyboardScrollController", "KeyboardCollectionController", "KeyboardRegion",
    "ReaderKeyboardArea", "ComicReaderKeyboardArea", "Keys.", "activeFocusOnTab",
)


def matching_brace(text: str, open_position: int) -> int:
    depth = 0
    quote = None
    escaped = False
    for index in range(open_position, len(text)):
        char = text[index]
        if quote:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == quote:
                quote = None
            continue
        if char in ('"', "'"):
            quote = char
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return index
    return len(text) - 1


def line_of(text: str, position: int) -> int:
    return text.count("\n", 0, position) + 1


def scan_file(path: Path, root: Path) -> list[dict]:
    text = path.read_text(encoding="utf-8", errors="replace")
    lines = text.splitlines()
    pattern = re.compile(r"\b(" + "|".join(SCROLL_TYPES) + r")\s*\{")
    relative = str(Path(root.name) / path.relative_to(root)).replace("\\", "/")
    rows = []
    for match in pattern.finditer(text):
        surface_type = match.group(1)
        open_position = text.find("{", match.start())
        end = matching_brace(text, open_position)
        block = text[match.start():end + 1]
        start_line = line_of(text, match.start())
        lo = max(0, start_line - 41)
        hi = min(len(lines), start_line + 40)
        nearby = "\n".join(lines[lo:hi])

        local = [token for token in KEYBOARD_OWNERS if token in block]
        nearby_owners = [token for token in KEYBOARD_OWNERS if token in nearby and token not in local]
        if local:
            classification = "COVERED"
            owner = f"local {local[0]} owns the scroll surface"
            evidence = [f"block:{token}" for token in local]
        elif nearby_owners:
            classification = "COVERED"
            owner = f"nearby {nearby_owners[0]} owns the scroll surface"
            evidence = [f"nearby:{token}" for token in nearby_owners]
        elif "ScrollGlide" in block or "ScrollGlide" in nearby:
            classification = "DELEGATED"
            owner = "ScrollGlide pointer mechanics delegated to the owning page"
            evidence = ["ScrollGlide"]
        else:
            classification = "EXCEPTION"
            owner = "pointer scroll surface has no independent semantic action"
            evidence = ["pointer scroll surface; keyboard ownership is at the parent route"]

        rows.append({
            "file": relative,
            "line": start_line,
            "surface_type": surface_type,
            "owner": owner,
            "classification": classification,
            "evidence": evidence,
        })
    return rows


def build_payload(root: Path) -> dict:
    files = sorted(root.rglob("*.qml"))
    rows = []
    for path in files:
        rows.extend(scan_file(path, root))
    rows.sort(key=lambda row: (row["file"], row["line"], row["surface_type"]))
    return {
        "schemaVersion": 1,
        "generatedFrom": "qml",
        "filesScanned": len(files),
        "surfaceTypes": list(SCROLL_TYPES),
        "stats": dict(sorted(Counter(row["classification"] for row in rows).items())),
        "rows": rows,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--qml-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    payload = build_payload(args.qml_root.resolve())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    print(f"KEYBOARD_SCROLL_CENSUS_OK files={payload['filesScanned']} rows={len(payload['rows'])} stats={payload['stats']}")


if __name__ == "__main__":
    main()
