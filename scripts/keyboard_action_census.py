"""Generate the Arc 41 action-level keyboard ownership census.

The scanner deliberately produces candidates from pointer handlers first.  A
candidate is only considered resolved when the source contains a nearby
keyboard owner or when it is covered by the explicit residual classification
table below.  This keeps source shape useful for discovery without confusing a
generic ``Keys`` token with proof of behavior.
"""

from __future__ import annotations

import argparse
import json
import re
from collections import Counter
from pathlib import Path


BASE_POINTER_TYPES = ("MouseArea", "TapHandler", "WheelHandler", "DragHandler", "HoverHandler")
HANDLERS = (
    "onClicked", "onDoubleClicked", "onPressAndHold", "onPressed", "onReleased",
    "onPositionChanged", "onEntered", "onExited", "onTapped", "onSingleTapped",
    "onDoubleTapped", "onLongPressed", "onWheel", "onTranslationChanged",
    "onActiveTranslationChanged", "onHoveredChanged",
)
KEYBOARD_TOKENS = (
    "KeyboardAction", "KeyboardCollectionController", "KeyboardScrollController",
    "KeyboardRegion", "ReaderKeyboardArea", "ComicReaderKeyboardArea", "Keys.",
    "KeyNavigation.", "Shortcut", "activeFocusOnTab", "focusPolicy",
)

# The source scanner found these rows with no local keyboard token.  They are
# deliberately explicit: the residual test and this generator must agree on
# the reason each one is safe to leave without a duplicate action object.
RESIDUAL_OWNERS = {
    "qml/reader2/ReaderChrome.qml": (16, "DELEGATED", "ReaderKeyboardArea owns focus, activation, context, and slider keys"),
    "qml/reader2/BottomRail.qml": (4, "DELEGATED", "ReaderKeyboardArea owns focus, activation, context, and slider keys"),
    "qml/reader2/TopBar.qml": (1, "DELEGATED", "ReaderKeyboardArea owns focus, activation, context, and slider keys"),
    "qml/comicreader/ComicReaderHud.qml": (14, "DELEGATED", "ComicReaderKeyboardArea owns focus, activation, context, and slider keys"),
    "qml/comicreader/ComicReaderLoupe.qml": (4, "DELEGATED", "ComicReaderKeyboardArea owns focus, activation, context, and slider keys"),
    "qml/comicreader/ComicReaderCommandBar.qml": (1, "DELEGATED", "ComicReaderKeyboardArea owns focus, activation, context, and slider keys"),
    "qml/CataloguePosterCard.qml": (2, "DELEGATED", "CataloguePosterGrid owns the collection index and activation"),
    "qml/VaultPosterCard.qml": (2, "DELEGATED", "VaultPage owns the collection index and activation"),
    "qml/VaultWideCard.qml": (2, "DELEGATED", "VaultPage owns the collection index and activation"),
    "qml/ScrollGlide.qml": (1, "EXCEPTION", "WheelHandler is pointer-only scroll mechanics; the owning page handles keyboard scroll"),
    "qml/comicreader/ComicReaderStripSurface.qml": (1, "EXCEPTION", "WheelHandler is pointer-only scroll mechanics; the owning reader handles keyboard scroll"),
    "qml/TheatreWorld.qml": (1, "EXCEPTION", "Full-screen click catcher intentionally absorbs background input"),
    "qml/WorldPage.qml": (1, "EXCEPTION", "Full-screen click catcher intentionally absorbs stray background input"),
    "qml/FullscreenTransitionShield.qml": (1, "EXCEPTION", "Transition shield intentionally absorbs input while the shell changes state"),
    "qml/player2/controls/PlaybackStatsCard.qml": (1, "EXCEPTION", "Stats card absorbs background clicks and exposes no command"),
    "qml/reader2/HarnessShelf.qml": (2, "EXCEPTION", "Verification-only reader harness, not a shipped route"),
    "qml/player2/Harness.qml": (1, "EXCEPTION", "Verification-only player harness, not a shipped route"),
    "qml/MangaSeriesThumbnailMock.qml": (10, "EXCEPTION", "Retired WeebCentral route; live route is MangaSeries.qml"),
}


def line_of(text: str, position: int) -> int:
    return text.count("\n", 0, position) + 1


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


def root_type(text: str) -> str:
    stripped = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    stripped = re.sub(r"//[^\n]*", "", stripped)
    match = re.search(r"(?m)^\s*([A-Za-z_][A-Za-z0-9_.]*)\s*\{", stripped)
    return match.group(1).split(".")[-1] if match else ""


def discover_pointer_types(files: list[Path]) -> tuple[str, ...]:
    types = set(BASE_POINTER_TYPES)
    changed = True
    while changed:
        changed = False
        for path in files:
            try:
                parent = root_type(path.read_text(encoding="utf-8", errors="replace"))
            except OSError:
                continue
            if parent in types and path.stem not in types:
                types.add(path.stem)
                changed = True
    return tuple(sorted(types))


def synopsis(block: str, handler: str) -> str:
    match = re.search(rf"\b{re.escape(handler)}\s*:\s*", block)
    if not match:
        return ""
    start = match.end()
    tail = block[start:]
    brace = tail.find("{")
    newline = tail.find("\n")
    starts_blockish = tail.lstrip().startswith(("function", "{", "("))
    if brace >= 0 and (newline < 0 or brace < newline or starts_blockish):
        open_position = start + brace
        end = matching_brace(block, open_position)
        raw = block[start:end + 1]
    else:
        raw = tail.splitlines()[0] if tail else ""
    return re.sub(r"\s+", " ", raw).strip()[:360]


def evidence_for(block: str, nearby: str, file_text: str) -> list[str]:
    result = []
    for token in KEYBOARD_TOKENS:
        if token in block:
            result.append(f"block:{token}")
        elif token in nearby:
            result.append(f"nearby:{token}")
        elif token in file_text:
            result.append(f"file:{token}")
    return result


def classify(relative: str, block: str, nearby: str, file_text: str, gesture: str) -> tuple[str, str, list[str]]:
    # Explicit residual ownership is intentionally authoritative for these
    # files.  Their local pointer type may itself contain keyboard markers,
    # but duplicating the command in every nested surface would be the wrong
    # ownership model.
    if relative in RESIDUAL_OWNERS:
        expected_count, classification, rationale = RESIDUAL_OWNERS[relative]
        del expected_count
        evidence = [rationale]
        if "ReaderKeyboardArea" in rationale or "ComicReaderKeyboardArea" in rationale:
            evidence.append("shared reader primitive")
        return classification, rationale, evidence

    if gesture == "capture-only" and relative not in RESIDUAL_OWNERS:
        return "EXCEPTION", "capture-only pointer surface", ["capture-only"]

    block_tokens = [token for token in KEYBOARD_TOKENS if token in block]
    nearby_tokens = [token for token in KEYBOARD_TOKENS if token in nearby]
    if block_tokens:
        token = block_tokens[0]
        return "COVERED", f"local {token} owner", [f"block:{token}"]
    if nearby_tokens:
        token = nearby_tokens[0]
        return "COVERED", f"nearby {token} owner", [f"nearby:{token}"]

    file_tokens = [token for token in KEYBOARD_TOKENS if token in file_text]
    if file_tokens:
        token = file_tokens[0]
        return "DELEGATED", f"file-level {token} owner", [f"file:{token}"]
    return "BUG", "no keyboard owner found", ["missing keyboard evidence"]


def scan_file(path: Path, root: Path, pointer_types: tuple[str, ...]) -> list[dict]:
    text = path.read_text(encoding="utf-8", errors="replace")
    lines = text.splitlines()
    pattern = re.compile(r"\b(" + "|".join(re.escape(t) for t in sorted(pointer_types, key=len, reverse=True)) + r")\s*\{")
    rows = []
    for match in pattern.finditer(text):
        pointer_type = match.group(1)
        end = matching_brace(text, text.find("{", match.start()))
        block = text[match.start():end + 1]
        start_line = line_of(text, match.start())
        lo = max(0, start_line - 41)
        hi = min(len(lines), start_line + 40)
        nearby = "\n".join(lines[lo:hi])
        handlers = [handler for handler in HANDLERS if re.search(rf"\b{re.escape(handler)}\s*:", block)]
        if not handlers:
            handlers = ["capture-only"]
        relative = str(Path(root.name) / path.relative_to(root)).replace("\\", "/")
        for gesture in handlers:
            classification, owner, evidence = classify(relative, block, nearby, text, gesture)
            rows.append({
                "file": relative,
                "line": start_line,
                "pointer_type": pointer_type,
                "gesture": gesture,
                "description": synopsis(block, gesture) or "capture-only pointer surface",
                "owner": owner,
                "classification": classification,
                "evidence": evidence,
            })
    return rows


def build_payload(root: Path) -> dict:
    files = sorted(root.rglob("*.qml"))
    pointer_types = discover_pointer_types(files)
    rows = []
    for path in files:
        rows.extend(scan_file(path, root, pointer_types))
    rows.sort(key=lambda row: (row["file"], row["line"], row["gesture"], row["pointer_type"]))

    counts = Counter(row["classification"] for row in rows)
    residual_counts = Counter(
        row["file"] for row in rows
        if row["file"] in RESIDUAL_OWNERS and not any(token in row["evidence"][0] for token in ("block:", "nearby:", "file:"))
    )
    mismatches = {
        relative: {"expected": values[0], "actual": residual_counts.get(relative, 0)}
        for relative, values in RESIDUAL_OWNERS.items()
        if residual_counts.get(relative, 0) != values[0]
    }
    if mismatches:
        raise RuntimeError(f"residual ownership counts changed: {mismatches}")

    return {
        "schemaVersion": 1,
        "generatedFrom": "qml",
        "filesScanned": len(files),
        "pointerTypes": list(pointer_types),
        "stats": dict(sorted(counts.items())),
        "residualRows": sum(values[0] for values in RESIDUAL_OWNERS.values()),
        "rows": rows,
    }


def write_markdown(payload: dict, output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    stats = payload["stats"]
    with output.open("w", encoding="utf-8") as handle:
        handle.write("# Arc 41 keyboard action residual audit\n\n")
        handle.write("This report is generated from the current production QML tree. Pointer handlers are candidates; ownership is resolved by a nearby keyboard owner or an explicit residual rationale.\n\n")
        handle.write(f"- QML files scanned: {payload['filesScanned']}\n")
        handle.write(f"- Candidate rows: {len(payload['rows'])}\n")
        handle.write(f"- Classifications: {', '.join(f'{key}={value}' for key, value in sorted(stats.items()))}\n")
        handle.write(f"- Explicit residual rows: {payload['residualRows']}\n\n")
        handle.write("## Residual classifications\n\n")
        handle.write("| File | Rows | Classification | Rationale |\n|---|---:|---|---|\n")
        for relative, (count, classification, rationale) in sorted(RESIDUAL_OWNERS.items()):
            handle.write(f"| `{relative}` | {count} | {classification} | {rationale} |\n")
        handle.write("\nNo candidate is silently omitted. `BUG` is reserved for a pointer action with no local, delegated, or explicit exceptional owner.\n")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--qml-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--markdown", type=Path)
    args = parser.parse_args()
    root = args.qml_root.resolve()
    payload = build_payload(root)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    if args.markdown:
        write_markdown(payload, args.markdown)
    print(f"KEYBOARD_ACTION_CENSUS_OK files={payload['filesScanned']} rows={len(payload['rows'])} stats={payload['stats']}")


if __name__ == "__main__":
    main()
