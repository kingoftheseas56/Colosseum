# test_scroll_glide_p0 — ScrollGlide architecture + contract gate (2026-08-02 overhaul).
#
# The shared wheel controller was rebuilt: the old fixed 420ms NumberAnimation (restarted on
# every wheel event) is gone, replaced by a velocity accumulator drained by a FrameAnimation.
# This gate checks the load + behaviour of the new shape and locks the unified wiring.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$qmlExe = "C:\Qt\6.11.1\msvc2022_64\bin\qml.exe"
if (!(Test-Path $qmlExe)) { throw "qml.exe not found at $qmlExe - update the Qt path in this test." }

# ── 1. the new architecture: load + behavioural harness (verdict rides the exit code) ──
$harness = Join-Path $PSScriptRoot "scroll_glide_harness.qml"
$out = cmd /c "`"$qmlExe`" -platform offscreen `"$harness`" 2>&1" | Out-String
if ($LASTEXITCODE -ne 0) { throw "scroll_glide harness failed (exit $LASTEXITCODE):`n$out" }
Write-Host "scroll_glide harness PASS"

# A real QtTest wheel injection covers the event-delivery path that a synchronous math harness
# cannot reach. The component must receive a mouse wheel at the actual Flickable surface.
$env:QT_QPA_PLATFORM = "offscreen"
$env:QT_FORCE_STDERR_LOGGING = "1"
$wheelTest = Join-Path $root "tests\qml\tst_scroll_glide_wheel.qml"
$qmlTestRunner = "C:\Qt\6.11.1\msvc2022_64\bin\qmltestrunner.exe"
$wheelOut = & $qmlTestRunner -input $wheelTest -platform offscreen 2>&1 | Out-String
if ($LASTEXITCODE -ne 0) { throw "scroll_glide wheel delivery test failed (exit $LASTEXITCODE):`n$wheelOut" }
if ($wheelOut -notlike "*test_mouse_wheel_reaches_shared_controller()*") {
    throw "scroll_glide wheel delivery test did not run its real-event assertion.`n$wheelOut" }
Write-Host "scroll_glide wheel delivery PASS"

# ── 2. the pure-logic math gate (notch distance, settle time, burst, trackpad, clamp) ──
$mathOut = & node (Join-Path $PSScriptRoot "scroll_glide_math_test.mjs") 2>&1 | Out-String
if ($LASTEXITCODE -ne 0) { throw "scroll_glide math test failed (exit $LASTEXITCODE):`n$mathOut" }
Write-Host "scroll_glide math PASS"

# ── 3. the shared component carries the new architecture, not the old one ──
$sg = Get-Content (Join-Path $root "qml/ScrollGlide.qml") -Raw
if ($sg -notlike "*FrameAnimation*") { throw "ScrollGlide must drain via FrameAnimation (no fixed-duration tail)." }
if ($sg -notlike "*_pending*")        { throw "ScrollGlide must keep a velocity accumulator (_pending)." }
if ($sg -like "*duration: 420*")      { throw "ScrollGlide must NOT keep the old fixed 420ms wheel tail." }
if ($sg -notmatch "drainFraction\s*:\s*0\.38") { throw "ScrollGlide must preserve reader-parity drainFraction 0.38." }
if ($sg -notmatch "maxBacklogPx\s*:\s*6000") { throw "ScrollGlide must bound queued wheel backlog at 6000px." }
if ($sg -notlike "*frameTime*")       { throw "ScrollGlide must compensate motion using FrameAnimation.frameTime." }
if ($sg -notlike "*pixelDelta*")      { throw "ScrollGlide must preserve native trackpad pixel deltas." }
if ($sg -notlike "*angleDelta*")      { throw "ScrollGlide must preserve mouse-wheel angle fallback." }
if ($sg -notlike "*e.angleDelta.y * glide.speed*") {
    throw "ScrollGlide angle fallback must preserve the public speed multiplier." }
if ($sg -notlike "*acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad*") {
    throw "ScrollGlide must accept Mouse + TouchPad through one WheelHandler (no double-handling)." }
if ($sg -notlike "*e.accepted = true*"){ throw "ScrollGlide must accept handled wheel events (suppress default)." }
if ($sg -notlike "*_pendingPx*")      { throw "ScrollGlide must keep a floating pending backlog." }
if ($sg -notlike "*_drainFresh*")     { throw "ScrollGlide must protect the first frame after idle." }
if ($sg -notlike "*_draining*")       { throw "ScrollGlide must distinguish self-authored contentY writes." }

# ── 4. the catalogue surfaces are unified on the shared controller ──
# DiscoverBrowser wall + CataloguePosterGrid previously relied on Qt's default GridView wheel;
# both now attach ScrollGlide so landing, Discover, and See-all share one feel.
$db = Get-Content (Join-Path $root "qml/DiscoverBrowser.qml") -Raw
if ($db -notlike "*ScrollGlide*")     { throw "DiscoverBrowser must attach ScrollGlide to its wall." }
$cg = Get-Content (Join-Path $root "qml/CataloguePosterGrid.qml") -Raw
if ($cg -notlike "*ScrollGlide*")     { throw "CataloguePosterGrid must attach ScrollGlide." }
# horizontal PosterRail scrolling is NOT touched by this arc — guard against an accidental attach
foreach ($rail in @("PosterRail")) {
    $p = Join-Path $root "qml/$rail.qml"
    if (Test-Path $p) {
        if ((Get-Content $p -Raw) -like "*ScrollGlide*") { throw "$rail (horizontal) must NOT carry ScrollGlide." }
    }
}

# ── 5. the existing representative ScrollGlide consumers still attach it (no regression) ──
foreach ($f in @("WorldPage","DownloadsPage","ComicSeries","SearchSurface")) {
    $c = Get-Content (Join-Path $root "qml/$f.qml") -Raw
    if ($c -notlike "*ScrollGlide*")    { throw "$f missing ScrollGlide (regression)" }
    if ($c -notlike "*HouseScrollBar*") { throw "$f missing HouseScrollBar (regression)" }
    if ($c -notlike "*pixelAligned: false*") {
        throw "$f shared vertical ScrollGlide surface must explicitly preserve sub-pixel motion." }
}

# Current HEAD also wires the two vertical GridView catalogue walls through ScrollGlide. Keep their
# sub-pixel contract explicit while avoiding any horizontal PosterRail changes.
foreach ($f in @("DiscoverBrowser","CataloguePosterGrid")) {
    $c = Get-Content (Join-Path $root "qml/$f.qml") -Raw
    if ($c -notlike "*ScrollGlide*") { throw "$f missing ScrollGlide (regression)" }
    if ($c -notlike "*pixelAligned: false*") {
        throw "$f shared vertical GridView must explicitly preserve sub-pixel motion." }
}

# ── 6. HouseScrollBar current contract (the old harness asserted internals that no longer exist) ──
# The real contract today (commit 61dde27): an always-on, interactive, gold-under-hand proper
# thumb. Assert what is actually true rather than the retired motion-revealed sliver.
$bar = Get-Content (Join-Path $root "qml/HouseScrollBar.qml") -Raw
if ($bar -notlike "*ScrollBar {*")             { throw "HouseScrollBar must be a ScrollBar." }
if ($bar -notlike "*interactive: true*")       { throw "HouseScrollBar thumb must be grabbable (interactive)." }
if ($bar -notlike "*policy:*")                 { throw "HouseScrollBar must set a present-when-overflowing policy." }
if ($bar -notlike "*Qt.rgba(0.94, 0.77, 0.29*"){ throw "HouseScrollBar thumb must go gold under the hand (pressed)." }
if ($bar -like  "*Qt.rgba(1, 1, 1, 0.46)*")    { throw "HouseScrollBar must not paint the old white hover thumb." }

Write-Host "test_scroll_glide_p0 PASS"
