# Comic Reader — FAMILY GRADIENT HUD + INPUT gate (Task 11).
#
# Drives qml/comicreader/ComicReaderHud.qml + ComicReaderInput.qml (+ ComicReaderIcon.qml) offscreen
# through comicreader_chrome_harness.qml and asserts the HUD + input LOGIC: scrub ratio<->page
# mapping (double unit-snap / strip fraction), bookmark ticks, pair-aware counter, mode/direction
# chips writing the persisted seams, prev/next boundary gating, 3s auto-hide, the semantic key map,
# direction-aware click zones, the 220ms single-vs-double disambiguation, and the magnified drag
# cancel. The pixel look is Hemanth's eyes-on (Task 14); this pins BEHAVIOR.
#
# Plus STATIC guards PowerShell can do that qml.exe cannot reliably:
#   * no guided import/reference in any of the three chrome QML files (Guided is frozen elsewhere);
#   * every vendored icon SVG under assets/icons/comicreader/ has stroke="#ffffff" (MultiEffect
#     colorization law — a black-stroke SVG colorizes to black = invisible);
#   * the HUD carries NO text-glyph chips: no arrow/chevron GLYPH characters as text (the
#     semantic-icon-audit law — every navigational glyph must be a ComicReaderIcon).
#
# qml.exe is located exactly as every sibling tests/test_*.ps1 does (hardcoded Qt path); -I
# tests/qmlmock mirrors the sibling harnesses.

$ErrorActionPreference = "Stop"

$qmlExe = "C:/Qt/6.11.1/msvc2022_64/bin/qml.exe"
if (!(Test-Path -LiteralPath $qmlExe)) {
    Write-Host "FAIL: qml.exe not found at $qmlExe"
    exit 1
}

$chromeFiles = @(
    (Join-Path $PSScriptRoot "../qml/comicreader/ComicReaderHud.qml"),
    (Join-Path $PSScriptRoot "../qml/comicreader/ComicReaderCommandBar.qml"),
    (Join-Path $PSScriptRoot "../qml/comicreader/ComicReaderInput.qml"),
    (Join-Path $PSScriptRoot "../qml/comicreader/ComicReaderIcon.qml")
)

# --- static: files exist + NO guided import/reference anywhere in the chrome ---
foreach ($f in $chromeFiles) {
    if (!(Test-Path -LiteralPath $f)) {
        Write-Host "FAIL: chrome file not found at $f"
        exit 1
    }
    $guidedHits = Select-String -LiteralPath $f -Pattern "guided" -SimpleMatch -CaseSensitive:$false
    if ($guidedHits) {
        Write-Host "FAIL: $f must contain NO guided reference; found:"
        $guidedHits | ForEach-Object { Write-Host ("  line " + $_.LineNumber + ": " + $_.Line.Trim()) }
        exit 1
    }
}

# --- static: every vendored comicreader icon SVG must have stroke="#ffffff" ---
$iconDir = Join-Path $PSScriptRoot "../assets/icons/comicreader"
if (!(Test-Path -LiteralPath $iconDir)) {
    Write-Host "FAIL: comicreader icon dir not found at $iconDir"
    exit 1
}
$svgs = Get-ChildItem -LiteralPath $iconDir -Filter *.svg -File
if ($svgs.Count -lt 8) {
    Write-Host ("FAIL: expected >=8 vendored comicreader icons, found " + $svgs.Count)
    exit 1
}
foreach ($svg in $svgs) {
    $white = Select-String -LiteralPath $svg.FullName -Pattern 'stroke="#ffffff"' -SimpleMatch -CaseSensitive:$false
    if (-not $white) {
        Write-Host ("FAIL: " + $svg.Name + " must carry stroke=""#ffffff"" (MultiEffect colorization law)")
        exit 1
    }
}

# --- static: the visual chrome carries NO text-glyph chips (arrow/chevron GLYPH chars as text) ---
# guillemets, angle-bracket glyphs, unicode arrows, triangles used as nav glyphs
$arrowGlyphs = @([char]0x2039, [char]0x203A, [char]0x00AB, [char]0x00BB, [char]0x2190, [char]0x2192, [char]0x2191, [char]0x2193, [char]0x25B6, [char]0x25C0, [char]0x27E8, [char]0x27E9)
foreach ($f in @($chromeFiles[0], $chromeFiles[1])) {
    $vt = Get-Content -LiteralPath $f -Raw
    foreach ($g in $arrowGlyphs) {
        if ($vt.Contains([string]$g)) {
            Write-Host ("FAIL: {0} contains a text arrow glyph U+{1:X4} - every nav glyph must be a ComicReaderIcon (semantic-icon-audit law)" -f (Split-Path $f -Leaf), [int]$g)
            exit 1
        }
    }
}

# --- static: THE READER HAS NO SIDEBAR ---
# Hemanth's own correction of the first flat-chrome mock, verbatim: "colosseum does not have a side
# panel we can use inside the reader". The approved chrome is a thin title strip, one flat command
# bar, and one gold rail; the comic runs edge to edge under them. A behavioural assertion alone
# (byName(hud,"readerSidebar") === null) can be satisfied by a sidebar that simply omits the marker
# objectName, so the word itself is banned from the chrome sources too.
foreach ($f in $chromeFiles) {
    $sideHits = Select-String -LiteralPath $f -Pattern "readerSidebar" -SimpleMatch -CaseSensitive:$false
    if ($sideHits) {
        Write-Host "FAIL: $f references a reader sidebar; the approved reader has none."
        $sideHits | ForEach-Object { Write-Host ("  line " + $_.LineNumber + ": " + $_.Line.Trim()) }
        exit 1
    }
}

# --- static: the chrome sleeps after 2.5 seconds, not 3 ---
# "Toolbar, title toast, progress rail, and cursor sleep together after 2.5 seconds of inactivity."
# The HUD's dial and the shell's cursor dial are two separate numbers that must agree, and the
# offscreen harness can only see the HUD's - so the shell's is pinned here.
$shellQml = Join-Path $PSScriptRoot "../qml/comicreader/ComicReaderShell.qml"
$cursorDial = Select-String -LiteralPath $shellQml -Pattern "property int cursorIdleMs:\s*2500"
if (!$cursorDial) {
    Write-Host "FAIL: ComicReaderShell.qml must sleep the CURSOR at 2500ms, together with the chrome."
    exit 1
}

$env:QT_FORCE_STDERR_LOGGING = "1"
$harness  = Join-Path $PSScriptRoot "comicreader_chrome_harness.qml"
$mockPath = Join-Path $PSScriptRoot "qmlmock"

$prevEAP = $ErrorActionPreference
$ErrorActionPreference = "Continue"
$output = & $qmlExe -platform offscreen -I $mockPath $harness 2>&1 | Out-String
$code = $LASTEXITCODE
$ErrorActionPreference = $prevEAP

# --- a positioner warning means a control is silently DEAD, so it fails the gate ---
# Qt refuses fill/centerIn anchors on a child of a Row/Column and says so, then gives that child
# ZERO size. A MouseArea that loses its size stops receiving clicks while still looking perfect on
# screen: exactly how the reader shipped a "Back to Library" button that did nothing for a day
# (Hemanth, 2026-07-26 — Escape worked, the button never had). Qt printed this warning on every
# run the whole time and it scrolled past, because the behavioural assertions were all green.
# It is not noise. It is a control that does not work.
if ($output -match "Cannot specify left, right, horizontalCenter, fill or centerIn anchors for items inside (Row|Column)") {
    Write-Host "FAIL: a positioner-anchor warning was emitted - some item inside a Row/Column has"
    Write-Host "      fill/centerIn anchors. Qt gives that item ZERO size, so if it is a MouseArea"
    Write-Host "      the control it belongs to is dead to clicks while still rendering fine."
    Write-Host $output
    exit 1
}

if ($code -ne 0 -or ($output -notmatch "COMICREADER_CHROME_OK")) {
    Write-Host "FAIL: comic reader chrome offscreen harness (exit $code)"
    Write-Host $output
    exit 1
}

# --- physical-click regression: Back/Minimize/Fullscreen/Close and the command bar against the
#     REAL ComicReaderHud.qml, using genuine QtTest mouseClick() coordinate hit-testing. The
#     offscreen harness above calls .tapped()/.triggered() directly and cannot see a geometry-
#     overlap bug (the page-turn strips used to sit on top of Back/Close/Fullscreen/the command
#     bar) — this is the only thing in this repo that can prove or regress it. Proven red before
#     the fix, green after, per its own header comment.
$qmlTestRunner = "C:/Qt/6.11.1/msvc2022_64/bin/qmltestrunner.exe"
$titleControlTest = Join-Path $PSScriptRoot "qml/tst_comicreader_title_controls.qml"
$titleOutput = & $qmlTestRunner -platform offscreen -input $titleControlTest 2>&1 | Out-String
if ($LASTEXITCODE -ne 0) {
    Write-Host "FAIL: comic reader title control click-reliability regression"
    Write-Host $titleOutput
    exit 1
}

Write-Host "COMICREADER_CHROME_OK"
