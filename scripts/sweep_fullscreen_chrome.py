#!/usr/bin/env python3
"""One-shot chrome sweep (2026-07-20): the fullscreen-only rule is dead — every page's
window cluster becomes minimize + fullscreen-toggle + power.

- 20 uniform SVG pages: insert a fullscreen toggle Item between the minimize and
  power Items, + `signal fullscreenRequested()` beside the existing signals.
- ExtensionsPage (text-glyph row): insert a glyph Text between the minimize and
  power glyphs.
- BiblioBook (Repeater model): third model entry + dispatch branch; dead-rule
  comment replaced.
- 3 gap pages (ContinueSeeAllPage/SearchSurface/BiblioSearch): append the full
  canonical SVG cluster (they had no window buttons at all).
- Main.qml: connect fullscreenRequested -> win.toggleFullscreenShell after every
  minimizeShell connect.

Every mutation asserts its anchor matched exactly once; any miss aborts the whole
run with the file untouched (fail loud, never half-sweep).
"""
import re
import sys
from pathlib import Path

QML = Path(__file__).resolve().parent.parent / "qml"

UNIFORM = [
    "GenrePage", "GenreIndex", "BiblioGenreIndex", "BiblioGenrePage",
    "TheatreGenrePage", "TheatreGenreIndex", "MangaSeries", "ComicSeries",
    "ComicSeriesPage", "LocgPublisherPage", "ComicArchiveBoard", "ComicArchiveIndex",
    "TheatreSeries", "DownloadsPage", "UniverseHallPage", "GalaxyUniversePage",
    "SagaUniversePage", "EraUniversePage", "StudioUniversePage", "UniversePage",
]
GAP = {"ContinueSeeAllPage": "root", "SearchSurface": "surf", "BiblioSearch": "search"}

def die(msg):
    print(f"ABORT: {msg}")
    sys.exit(1)

def add_signal(text, fname):
    """Insert `signal fullscreenRequested()` after the minimizeRequested declaration."""
    if "signal fullscreenRequested()" in text:
        die(f"{fname}: fullscreenRequested already declared")
    pat = re.compile(r"^([ \t]*)signal minimizeRequested\(\)[ \t]*$", re.M)
    hits = pat.findall(text)
    if len(hits) != 1:
        die(f"{fname}: expected exactly one 'signal minimizeRequested()' line, found {len(hits)}")
    return pat.sub(lambda m: f"{m.group(0)}\n{m.group(1)}signal fullscreenRequested()", text, count=1)

def toggle_item_block(indent, root_id):
    i = indent
    return (
        f"{i}Item {{\n"
        f"{i}    width: 22\n"
        f"{i}    height: 22\n"
        f"{i}    Image {{\n"
        f"{i}        anchors.fill: parent\n"
        f"{i}        source: (typeof WindowMode !== \"undefined\" && WindowMode.shellWindowed)\n"
        f"{i}                ? \"../assets/icons/fullscreen.svg\"\n"
        f"{i}                : \"../assets/icons/fullscreen-exit.svg\"\n"
        f"{i}        sourceSize.width: 22\n"
        f"{i}        sourceSize.height: 22\n"
        f"{i}        fillMode: Image.PreserveAspectFit\n"
        f"{i}        opacity: fsMa.containsMouse ? 1.0 : 0.72\n"
        f"{i}    }}\n"
        f"{i}    MouseArea {{\n"
        f"{i}        id: fsMa\n"
        f"{i}        anchors.fill: parent\n"
        f"{i}        hoverEnabled: true\n"
        f"{i}        cursorShape: Qt.PointingHandCursor\n"
        f"{i}        onClicked: {root_id}.fullscreenRequested()\n"
        f"{i}    }}\n"
        f"{i}}}\n"
    )

def toggle_compact_block(indent, root_id):
    i = indent
    return (
        f'{i}Image {{ source: (typeof WindowMode !== "undefined" && WindowMode.shellWindowed) ? "../assets/icons/fullscreen.svg" : "../assets/icons/fullscreen-exit.svg"; width: 17; height: 17; opacity: 0.7\n'
        f"{i}        MouseArea {{ anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: {root_id}.fullscreenRequested() }} }}\n"
    )

def sweep_uniform(fname):
    """Two stamp families: 'expanded' (22px Item blocks, TheatreSeries-style) and
    'compact' (17px two-line Image entries, GenrePage-style). Detect by what
    follows the minimize emission; insert the matching toggle before power."""
    p = QML / f"{fname}.qml"
    text = p.read_text(encoding="utf-8")
    if text.count("minimize.svg") != 1:
        die(f"{fname}: expected exactly one minimize.svg block")
    if "id: fsMa" in text:
        die(f"{fname}: fsMa id already taken")
    text = add_signal(text, fname)

    lines = text.splitlines(keepends=True)
    emit_idx = [n for n, l in enumerate(lines) if re.search(r"onClicked: (\w+)\.minimizeRequested\(\)", l)]
    if len(emit_idx) != 1:
        die(f"{fname}: expected exactly one minimizeRequested emission, found {len(emit_idx)}")
    root_id = re.search(r"onClicked: (\w+)\.minimizeRequested\(\)", lines[emit_idx[0]]).group(1)

    # compact: the line right after the emission is the power Image two-liner head
    m = re.match(r'^([ \t]*)Image \{ source: "\.\./assets/icons/power\.svg"', lines[emit_idx[0] + 1])
    if m:
        lines.insert(emit_idx[0] + 1, toggle_compact_block(m.group(1), root_id))
        p.write_text("".join(lines), encoding="utf-8", newline="")
        print(f"swept  {fname} (compact, root id: {root_id})")
        return

    # expanded: find the next sibling `Item {` opener — that's the power Item.
    insert_at = None
    for n in range(emit_idx[0] + 1, min(emit_idx[0] + 12, len(lines))):
        m = re.match(r"^([ \t]*)Item \{[ \t]*$", lines[n])
        if m:
            insert_at, indent = n, m.group(1)
            break
    if insert_at is None:
        die(f"{fname}: neither compact power line nor sibling Item found after the minimize emission")
    following = "".join(lines[insert_at:insert_at + 15])
    if "power.svg" not in following:
        die(f"{fname}: the Item after minimize is not the power block")

    lines.insert(insert_at, toggle_item_block(indent, root_id))
    p.write_text("".join(lines), encoding="utf-8", newline="")
    print(f"swept  {fname} (expanded, root id: {root_id})")

def sweep_extensions():
    p = QML / "ExtensionsPage.qml"
    text = p.read_text(encoding="utf-8")
    text = add_signal(text, "ExtensionsPage")
    anchor = ('            Text { text: "⏻"; color: pMa.containsMouse ? theme.ink : theme.inkDim; font.pixelSize: 17\n')
    if text.count(anchor) != 1:
        die("ExtensionsPage: power glyph anchor not found exactly once")
    block = (
        '            Text { text: "⛶"; color: fMa.containsMouse ? theme.ink : theme.inkDim; font.pixelSize: 17\n'
        "                   MouseArea { id: fMa; anchors.fill: parent; hoverEnabled: true\n"
        "                               cursorShape: Qt.PointingHandCursor; onClicked: root.fullscreenRequested() } }\n"
    )
    text = text.replace(anchor, block + anchor, 1)
    p.write_text(text, encoding="utf-8", newline="")
    print("swept  ExtensionsPage (text glyph row)")

def sweep_bibliobook():
    p = QML / "BiblioBook.qml"
    text = p.read_text(encoding="utf-8")
    text = add_signal(text, "BiblioBook")
    old_model = 'model: [ { g: "—", a: "min" }, { g: "⏻", a: "pow" } ]   // fullscreen-only: no maximize'
    new_model = 'model: [ { g: "—", a: "min" }, { g: "⛶", a: "fs" }, { g: "⏻", a: "pow" } ]   // min · fullscreen · power (fullscreen rule removed 2026-07-20)'
    if text.count(old_model) != 1:
        die("BiblioBook: system-glyph model anchor not found exactly once")
    text = text.replace(old_model, new_model, 1)
    old_dispatch = (
        '                            if (modelData.a === "min") detail.minimizeRequested()\n'
        '                            else if (modelData.a === "pow") detail.closeRequested()\n'
    )
    new_dispatch = (
        '                            if (modelData.a === "min") detail.minimizeRequested()\n'
        '                            else if (modelData.a === "fs") detail.fullscreenRequested()\n'
        '                            else if (modelData.a === "pow") detail.closeRequested()\n'
    )
    if text.count(old_dispatch) != 1:
        die("BiblioBook: dispatch anchor not found exactly once")
    text = text.replace(old_dispatch, new_dispatch, 1)
    p.write_text(text, encoding="utf-8", newline="")
    print("swept  BiblioBook (repeater model)")

def cluster_block(root_id):
    return (
        "\n"
        "    // window chrome (fullscreen rule removed 2026-07-20): the canonical\n"
        "    // minimize · fullscreen-toggle · power cluster every page carries.\n"
        "    Row {\n"
        "        z: 30\n"
        "        anchors.right: parent.right\n"
        "        anchors.rightMargin: theme.margin\n"
        "        y: 34\n"
        "        spacing: 20\n"
        "        Item {\n"
        "            width: 22\n"
        "            height: 22\n"
        "            Image {\n"
        "                anchors.fill: parent\n"
        "                source: \"../assets/icons/minimize.svg\"\n"
        "                sourceSize.width: 22\n"
        "                sourceSize.height: 22\n"
        "                fillMode: Image.PreserveAspectFit\n"
        "                opacity: chromeMinMa.containsMouse ? 1.0 : 0.72\n"
        "            }\n"
        "            MouseArea {\n"
        "                id: chromeMinMa\n"
        "                anchors.fill: parent\n"
        "                hoverEnabled: true\n"
        "                cursorShape: Qt.PointingHandCursor\n"
        f"                onClicked: {root_id}.minimizeRequested()\n"
        "            }\n"
        "        }\n"
        "        Item {\n"
        "            width: 22\n"
        "            height: 22\n"
        "            Image {\n"
        "                anchors.fill: parent\n"
        "                source: (typeof WindowMode !== \"undefined\" && WindowMode.shellWindowed)\n"
        "                        ? \"../assets/icons/fullscreen.svg\"\n"
        "                        : \"../assets/icons/fullscreen-exit.svg\"\n"
        "                sourceSize.width: 22\n"
        "                sourceSize.height: 22\n"
        "                fillMode: Image.PreserveAspectFit\n"
        "                opacity: fsMa.containsMouse ? 1.0 : 0.72\n"
        "            }\n"
        "            MouseArea {\n"
        "                id: fsMa\n"
        "                anchors.fill: parent\n"
        "                hoverEnabled: true\n"
        "                cursorShape: Qt.PointingHandCursor\n"
        f"                onClicked: {root_id}.fullscreenRequested()\n"
        "            }\n"
        "        }\n"
        "        Item {\n"
        "            width: 22\n"
        "            height: 22\n"
        "            Image {\n"
        "                anchors.fill: parent\n"
        "                source: \"../assets/icons/power.svg\"\n"
        "                sourceSize.width: 22\n"
        "                sourceSize.height: 22\n"
        "                fillMode: Image.PreserveAspectFit\n"
        "                opacity: chromePowMa.containsMouse ? 1.0 : 0.72\n"
        "            }\n"
        "            MouseArea {\n"
        "                id: chromePowMa\n"
        "                anchors.fill: parent\n"
        "                hoverEnabled: true\n"
        "                cursorShape: Qt.PointingHandCursor\n"
        f"                onClicked: {root_id}.closeRequested()\n"
        "            }\n"
        "        }\n"
        "    }\n"
    )

def sweep_gap(fname, root_id):
    p = QML / f"{fname}.qml"
    text = p.read_text(encoding="utf-8")
    if "minimize.svg" in text:
        die(f"{fname}: already has a minimize button?")
    for taken in ("id: fsMa", "id: chromeMinMa", "id: chromePowMa"):
        if taken in text:
            die(f"{fname}: id collision on {taken}")
    text = add_signal(text, fname)
    # Append the cluster before the file's final closing brace (last sibling ->
    # painted on top; z:30 belts it anyway).
    stripped = text.rstrip()
    if not stripped.endswith("}"):
        die(f"{fname}: unexpected file tail")
    text = stripped[:-1] + cluster_block(root_id) + "}\n"
    p.write_text(text, encoding="utf-8", newline="")
    print(f"swept  {fname} (gap page, cluster appended, root id: {root_id})")

def sweep_main():
    p = QML / "Main.qml"
    text = p.read_text(encoding="utf-8")
    pat = re.compile(r"^([ \t]*)item\.minimizeRequested\.connect\(win\.minimizeShell\)[ \t]*$", re.M)
    hits = pat.findall(text)
    if len(hits) != 19:
        die(f"Main.qml: expected 19 minimizeShell connects, found {len(hits)}")
    text = pat.sub(lambda m: f"{m.group(0)}\n{m.group(1)}item.fullscreenRequested.connect(win.toggleFullscreenShell)", text)
    p.write_text(text, encoding="utf-8", newline="")
    print(f"wired  Main.qml ({len(hits)} loaders)")

def main():
    for f in UNIFORM:
        sweep_uniform(f)
    sweep_extensions()
    sweep_bibliobook()
    for f, rid in GAP.items():
        sweep_gap(f, rid)
    sweep_main()
    print("SWEEP OK")

if __name__ == "__main__":
    main()
