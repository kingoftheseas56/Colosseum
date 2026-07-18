# One Piece Road Poneglyph Chamber Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the current One Piece Captain's Chart with a minimal-prose Road Poneglyph Chamber that preserves every provider identity and Theatre/Biblio route.

**Architecture:** `OnePieceUniversePage.qml` remains a bespoke, self-contained page fed only by the curated One Piece record in `Universes.js`. The root owns data lookup, scroll destinations, and route payloads; four focused inline components render stones, portals, manga gates, and film frames. A new grep-level P0 contract guards the visual/copy reset while the existing QML harness guards provider data and route behavior.

**Tech Stack:** Qt 6.11.1 QML/Qt Quick Controls, Canvas, PowerShell contract tests, offscreen `qml.exe` harness.

## Global Constraints

- Replace the visible page completely; do not retain the chart, compass, islands, ship's log, wanted posters, nautical-blue palette, or associated copy.
- The hero may show the sourced Wikipedia lead at two lines maximum; below it use only labels, titles, years, counts, and actions.
- `Universes.js` remains the only curation source; do not modify its provider pins.
- Preserve `{id,type,title}` Theatre payloads and the current Biblio title signal.
- Missing art leaves an honest basalt slot and never substitutes another work.
- Only touch `qml/OnePieceUniversePage.qml`, `tests/onepiece_page_load_harness.qml`, and `tests/test_onepiece_universe_p0.ps1` during implementation.

---

## File map

- Modify `qml/OnePieceUniversePage.qml`: complete visual and structural replacement.
- Modify `tests/onepiece_page_load_harness.qml`: retain data assertions and add the chamber-facing root contract.
- Create `tests/test_onepiece_universe_p0.ps1`: require new structure and ban retired lineage/copy.

### Task 1: Lock the replacement contract red

**Files:**
- Create: `tests/test_onepiece_universe_p0.ps1`
- Modify: `tests/onepiece_page_load_harness.qml`

**Interfaces:**
- Consumes: `OnePieceUniversePage.uni`, `poster(id)`, `watchSeries(pin)`, `watchMovie(pin)`.
- Produces: required `roomLabels`, `roomCount(route)`, and `scrollToRoom(route)` root interfaces.

- [ ] **Step 1: Add the failing P0 shape contract**

Create `tests/test_onepiece_universe_p0.ps1` with:

```powershell
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
function Read-File($rel) {
    $p = Join-Path $root $rel
    if (-not (Test-Path $p)) { throw "MISSING FILE: $rel" }
    Get-Content $p -Raw
}
function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") { throw $message }
}
function Assert-Lacks($text, $needle, $message) {
    if ($text -like "*$needle*") { throw $message }
}
$page = Read-File "qml/OnePieceUniversePage.qml"
foreach ($n in @('property var roomLabels', 'function roomCount(', 'function scrollToRoom(',
                 'component RoadStone', 'component MediaPortal', 'component MangaGate',
                 'component FilmFrame', 'WATCH', 'READ', 'FILMS', 'ADAPTATIONS',
                 '#8E1826', '#FFB35B', 'activeFocusOnTab: true',
                 'Keys.onReturnPressed', 'maximumLineCount: 2')) {
    Assert-Contains $page $n "One Piece chamber must carry: $n"
}
foreach ($n in @("THE CAPTAIN'S CHART", 'THE LOG POSE', "THE SHIP'S LOG",
                 'THE BOUNTY BOARD', 'SagaIsland', 'WantedPoster', 'nodeX(', 'nodeY(')) {
    Assert-Lacks $page $n "Retired One Piece lineage returned: $n"
}
$main = Read-File "qml/Main.qml"
Assert-Contains $main 'OnePieceUniversePage.qml' "Main must route One Piece to its bespoke page."
Write-Host "one piece universe p0: OK"
```

- [ ] **Step 2: Extend the load harness with the new root contract**

After `var u = p.uni`, add:

```qml
var roomsOk = p.roomLabels.length === 4
              && p.roomLabels[0] === "WATCH"
              && p.roomLabels[1] === "READ"
              && p.roomLabels[2] === "FILMS"
              && p.roomLabels[3] === "ADAPTATIONS"
              && p.roomCount("WATCH") === 1
              && p.roomCount("READ") === 8
              && p.roomCount("FILMS") === 17
              && p.roomCount("ADAPTATIONS") === 2
```

Add `&& roomsOk` to the final `ok` expression and include `roomsOk` in the failure message.

- [ ] **Step 3: Run both tests and prove red**

Run:

```powershell
powershell -NoProfile -File tests/test_onepiece_universe_p0.ps1
& 'C:\Qt\6.11.1\msvc2022_64\bin\qml.exe' -platform offscreen tests/onepiece_page_load_harness.qml
```

Expected: P0 fails on missing `roomLabels`; harness exits non-zero because `roomLabels` is undefined.

- [ ] **Step 4: Commit the red contract**

```powershell
git add tests/test_onepiece_universe_p0.ps1 tests/onepiece_page_load_harness.qml
git commit -m "test(universe): lock One Piece chamber contract"
```

### Task 2: Build the chamber hub and route rooms

**Files:**
- Modify: `qml/OnePieceUniversePage.qml`
- Test: `tests/test_onepiece_universe_p0.ps1`
- Test: `tests/onepiece_page_load_harness.qml`

**Interfaces:**
- Consumes: the current One Piece `Universes.js` record and root shell signals.
- Produces: `roomLabels`, `roomCount(route)`, `scrollToRoom(route)`, `RoadStone`, `MediaPortal`, `MangaGate`, `FilmFrame`.

- [ ] **Step 1: Replace the old page root and data helpers**

Preserve the existing shell signals, `reload`, `poster`, `watchSeries`, and `watchMovie`. Replace chart geometry and colors with:

```qml
readonly property color abyss: "#08070A"
readonly property color basalt: "#17131A"
readonly property color stoneRed: "#8E1826"
readonly property color carvedLight: "#FFB35B"
readonly property color bone: "#F2E6CF"
readonly property color seaGlass: "#5CB8B2"
property var roomLabels: ["WATCH", "READ", "FILMS", "ADAPTATIONS"]
property bool reducedMotion: false
function filmCount() {
    var n = 0
    for (var i = 0; i < (root.uni.filmEras || []).length; ++i)
        n += root.uni.filmEras[i].films.length
    return n
}
function roomCount(route) {
    if (route === "WATCH") return root.uni.anime ? 1 : 0
    if (route === "READ") return (root.uni.manga || []).length
    if (route === "FILMS") return filmCount()
    if (route === "ADAPTATIONS") return (root.uni.adaptations || []).length
    return 0
}
function scrollToRoom(route) {
    var target = route === "WATCH" ? watchRoom
               : route === "READ" ? readRoom
               : route === "FILMS" ? filmsRoom : adaptationsRoom
    page.contentY = Math.max(0, target.y - 24)
}
```

- [ ] **Step 2: Build the first viewport as the four-stone chamber**

Use one large hero item followed by a responsive `Grid` of four `RoadStone` instances. The title block contains exactly the universe name, the two-line sourced lead, and two small actions. Each stone receives `route`, `count`, and `onActivated: root.scrollToRoom(route)`.

The `RoadStone` interface is:

```qml
component RoadStone: FocusScope {
    id: stone
    property string route: ""
    property int count: 0
    signal activated()
    activeFocusOnTab: true
    Keys.onReturnPressed: activated()
    Keys.onEnterPressed: activated()
}
```

Draw abstract carved marks in an internal `Canvas`; do not draw readable invented glyphs. Use the red/light tokens verbatim and one hover/focus light sweep.

- [ ] **Step 3: Build the four catalog rooms**

Create four anchored room items with ids `watchRoom`, `readRoom`, `filmsRoom`, and `adaptationsRoom`.

- WATCH: one wide `MediaPortal` bound to `root.uni.anime` and `watchSeries`.
- READ: a wrapping `Flow` of eight `MangaGate` instances; the first is wider/taller.
- FILMS: one horizontal ribbon per `filmEras` record, each using `FilmFrame`.
- ADAPTATIONS: two wide `MediaPortal` instances bound to `watchSeries` and showing `UPCOMING` when present.

The component action surfaces are:

```qml
component MediaPortal: FocusScope {
    property var media: ({})
    property bool upcoming: false
    signal activated()
}
component MangaGate: FocusScope {
    property var manga: ({})
    property bool featured: false
    signal activated()
}
component FilmFrame: FocusScope {
    property var film: ({})
    signal activated()
}
```

Every component sets `activeFocusOnTab: true`, mirrors hover with focus, handles Return/Enter, and shows a basalt fallback with title when its `Image.status` is not `Image.Ready`.

- [ ] **Step 4: Add the single entrance sequence**

Animate only `contentColumn.opacity` plus each stone's short `y` offset, with successive delays capped below 320 ms. Bind every translation and delay duration to `root.reducedMotion ? 0 : <duration>`; opacity may resolve immediately when reduced motion is true. Do not add particles or perpetual animation.

- [ ] **Step 5: Run the focused gates green**

Run:

```powershell
powershell -NoProfile -File tests/test_onepiece_universe_p0.ps1
& 'C:\Qt\6.11.1\msvc2022_64\bin\qml.exe' -platform offscreen tests/onepiece_page_load_harness.qml
```

Expected: `one piece universe p0: OK`, harness prints `PASS`, both exit 0.

- [ ] **Step 6: Commit the replacement**

```powershell
git add qml/OnePieceUniversePage.qml
git commit -m "feat(universe): rebuild One Piece as Road Poneglyph chamber"
```

### Task 3: Regression verification and delivery

**Files:**
- Verify: `qml/OnePieceUniversePage.qml`
- Verify: `tests/test_onepiece_universe_p0.ps1`
- Verify: `tests/onepiece_page_load_harness.qml`

**Interfaces:**
- Consumes: completed page and test contracts.
- Produces: verified committed artifact ready for Hemanth's eyes-on acceptance.

- [ ] **Step 1: Run the shared provider-pin gate**

```powershell
powershell -NoProfile -File tests/test_universe_expansion_p0.ps1
```

Expected: exit 0 and its success line.

- [ ] **Step 2: Run QML static diagnostics**

```powershell
& 'C:\Qt\6.11.1\msvc2022_64\bin\qmllint.exe' qml/OnePieceUniversePage.qml
```

Expected: exit 0, or only pre-existing import-environment warnings documented verbatim.

- [ ] **Step 3: Verify the committed artifact**

```powershell
git diff --check HEAD~2..HEAD
git status --short
git log -2 --oneline
```

Expected: no whitespace errors; status contains only unrelated pre-existing brother work; the two One Piece commits are at HEAD.

- [ ] **Step 4: Push and hand to Hemanth for eyes-on**

```powershell
git push origin master
```

Report the exact gates run and explicitly leave visual acceptance to Hemanth's real-shell pass.
