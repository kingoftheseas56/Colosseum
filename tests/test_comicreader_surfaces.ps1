# Comic Reader — READING SURFACES gate (Task 10).
#
# Drives qml/comicreader/ComicReaderStripSurface.qml + ComicReaderDoubleSurface.qml offscreen
# through comicreader_surfaces_harness.qml and asserts the geometry/direction/feel logic the two
# surfaces own: strip virtualization + model-authoritative delegate height + coalesced viewport
# report + anti-jump compensation + the Tankoban-Max float wheel accumulator + per-page failure
# placards; double-page direction-aware x-order flip + spread-as-one-image + gutter shadow on pairs
# + zoom/pan clamp+reset + the unit-highest maxSeen mechanism.
#
# Plus a STATIC guard: neither surface may import anything under guided/ or reference a guided
# service (Guided is frozen and owned elsewhere). PowerShell can read the files directly, which
# qml.exe cannot do reliably — so the "no guided" assertion is a grep here, the behavior is the
# harness.
#
# qml.exe is located exactly as every sibling tests/test_*.ps1 does (hardcoded Qt path); -I
# tests/qmlmock mirrors the sibling harnesses (harmless here — the surfaces need no mock module).

$ErrorActionPreference = "Stop"

$qmlExe = "C:/Qt/6.11.1/msvc2022_64/bin/qml.exe"
if (!(Test-Path -LiteralPath $qmlExe)) {
    Write-Host "FAIL: qml.exe not found at $qmlExe"
    exit 1
}

# --- static assertion: NO guided import/reference in either surface ---
$surfaces = @(
    (Join-Path $PSScriptRoot "../qml/comicreader/ComicReaderStripSurface.qml"),
    (Join-Path $PSScriptRoot "../qml/comicreader/ComicReaderDoubleSurface.qml")
)
foreach ($s in $surfaces) {
    if (!(Test-Path -LiteralPath $s)) {
        Write-Host "FAIL: surface not found at $s"
        exit 1
    }
    $guidedHits = Select-String -LiteralPath $s -Pattern "guided" -SimpleMatch -CaseSensitive:$false
    if ($guidedHits) {
        Write-Host "FAIL: $s must contain NO guided reference; found:"
        $guidedHits | ForEach-Object { Write-Host ("  line " + $_.LineNumber + ": " + $_.Line.Trim()) }
        exit 1
    }
}

$env:QT_FORCE_STDERR_LOGGING = "1"
$harness  = Join-Path $PSScriptRoot "comicreader_surfaces_harness.qml"
$mockPath = Join-Path $PSScriptRoot "qmlmock"

# --- static: the two things that make the strip feel SMOOTH rather than harsh ---
# Both were got wrong once by re-deriving the drain instead of porting it, and neither is visible to
# a behavioural harness (a headless test can drive _drainWheel() by hand no matter what clocks it).
$strip = Get-Content -Raw -LiteralPath (Join-Path (Split-Path -Parent $PSScriptRoot) "qml/comicreader/ComicReaderStripSurface.qml")
if (-not $strip.Contains("FrameAnimation")) {
    Write-Host "FAIL: the strip scroll drain must be a FrameAnimation (vsync-locked)."
    Write-Host "      A Timer free-runs against the compositor, so steps double up or drop out and the"
    Write-Host "      column judders however good the easing curve is. This is the single biggest"
    Write-Host "      difference between the smooth lineage reader and a harsh one."
    exit 1
}
if ($strip -match "contentY\s*=\s*Math\.round") {
    Write-Host "FAIL: the strip must assign contentY the FLOAT accumulator, never Math.round(it)."
    Write-Host "      Rounding quantises the slow tail of every glide into stand-still-then-jump."
    exit 1
}

# --- static: every page image caps its DECODE size ---
# An uncapped Image decodes and uploads the page's full scan resolution (routinely 2000-3000px wide)
# to show it at a fraction of that, on the render thread, once per page arriving on screen. That is
# felt as smooth-but-occasionally-laggy rather than uniformly harsh, so a behavioural harness with
# fake 1x1 fixtures will never catch it. The lineage capped both surfaces; this reader did not.
$dbl = Get-Content -Raw -LiteralPath (Join-Path (Split-Path -Parent $PSScriptRoot) "qml/comicreader/ComicReaderDoubleSurface.qml")
foreach ($pair in @(@("strip", $strip), @("double", $dbl))) {
    if (-not ($pair[1] -match "sourceSize\.width")) {
        Write-Host ("FAIL: the " + $pair[0] + " surface must cap page decode with sourceSize.width -")
        Write-Host "      otherwise every page uploads its full scan resolution to be shown small."
        exit 1
    }
}

# --- static: the decode cap must not move when the WINDOW does ---
# sourceSize is part of an Image's cache key, so any change to the cap re-decodes every visible
# page. Deriving it from the viewport made entering fullscreen re-decode the whole column, and
# leaving it do so again - "going in and out of fullscreen looks incredibly rough" (Hemanth,
# 2026-07-26). It must derive from the SCREEN, which is the widest a page can ever be shown AND
# holds still while the window moves. No behavioural harness can see this: offscreen fixtures
# never change size.
if ($strip -notmatch "srcCapW[\s\S]{0,80}Screen\.width") {
    Write-Host "FAIL: the strip's srcCapW must derive from Screen.width, not the viewport."
    Write-Host "      A cap that tracks the window re-decodes every visible page on a resize,"
    Write-Host "      which is what made fullscreen transitions stutter."
    exit 1
}

# --- static: every page image KEEPS the pixmap cache ---
# The provider (ComicReaderProvider.cpp) re-runs a full-res scaledToWidth/SmoothTransformation on
# EVERY fetch because sourceSize only shrinks the decode for file-backed sources, not a provider's.
# cache: false forces that expensive downscale to re-pay on every delegate rebuild (scroll a few
# pages away and back); the ?rev= in the url self-busts the cache key on a genuine redecode, so
# cache: true is safe here and load-bearing for the "scroll back up and it stutters" cost.
foreach ($pair in @(@("strip", $strip), @("double", $dbl))) {
    if ($pair[1] -match "cache:\s*false") {
        Write-Host ("FAIL: the " + $pair[0] + " surface must keep cache: true on page Images -")
        Write-Host "      cache: false re-runs the provider's full-res downscale on every delegate rebuild."
        exit 1
    }
}

$prevEAP = $ErrorActionPreference
$ErrorActionPreference = "Continue"
$output = & $qmlExe -platform offscreen -I $mockPath $harness 2>&1 | Out-String
$code = $LASTEXITCODE
$ErrorActionPreference = $prevEAP

if ($code -ne 0 -or ($output -notmatch "COMICREADER_SURFACES_OK")) {
    Write-Host "FAIL: comic reader surfaces offscreen harness (exit $code)"
    Write-Host $output
    exit 1
}

Write-Host "COMICREADER_SURFACES_OK"
