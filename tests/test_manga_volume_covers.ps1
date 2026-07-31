# Gate for VOLUME COVERS on the tankoban shelf.
#
# A volume's cover is the first page of its FIRST chapter, scraped on demand exactly
# the way a chapter row gets its own thumbnail:
#   Downloads.fetchThumb(seriesId, chapterId) -> thumbReady(chapterId, url)
#
# Chapter thumbnails are NOT embedded in WeebCentral's chapter-list HTML. Looking
# there found no images, concluded per-volume covers were impossible, and shipped
# them hardcoded empty (2026-07-30) -- while the chapter rows right below were
# showing real thumbnails the whole time. This gate pins the wiring so that cannot
# happen silently again.
#
# ASCII only - PS 5.1 chokes on non-ASCII in a BOM-less .ps1.

$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$fail = 0

function Check($name, $ok) {
    if ($ok) { Write-Host "PASS  $name" }
    else { Write-Host "FAIL  $name"; $script:fail++ }
}

# 1. GREP SHAPE - the wiring strings must exist. A green grep proves presence, never
#    behaviour; the offscreen harness below proves behaviour.
$lib = Get-Content (Join-Path $repo "qml\MangaTankobanLibrary.qml") -Raw
Check "the shelf asks the downloader for a thumb" ($lib -match "fetchThumb\(root\.seriesId, cid\)")
Check "thumbReady routes back to the volume that asked" ($lib -match "function onThumbReady")
Check "a downloaded volume prefers its OWN first page" ($lib -match "localPages\(vid\)")

# 2. The page must hand the shelf the live chapter list, or there is nothing to
#    derive a cover FROM. This is the seam that silently breaks covers.
$series = Get-Content (Join-Path $repo "qml\MangaSeries.qml") -Raw
Check "MangaSeries feeds the shelf its chapter list" ($series -match "chapters: page\.chaptersModel")

# 2b. THE BURST + THE PERMANENT MISS (eyes-on 2026-07-31).
#     Asking for every volume queued ~115 WeebCentral scrapes on open; they run 3
#     at a time, the chapter rows' thumbs queued behind them, WeebCentral throttled,
#     and every failure was cached as an empty string FOREVER - so those volumes and
#     chapters showed numbered placeholders for the rest of the session.
Check "the shelf asks only for the page on screen" ($lib -match "rowsOnPage\(root\.activePage\)")
Check "turning the page fetches that page's covers" ($lib -match "onActivePageChanged: root\.requestCovers\(\)")
# Imperative code must not read the visibleRows BINDING: a change handler runs
# before dependent bindings re-evaluate, so it would see the page he just left.
Check "requestCovers resolves the page directly, not via the stale binding" `
    ($lib -notmatch "var rows = root\.visibleRows")

$dl = Get-Content (Join-Path $repo "native\engine\MangaDownloader.cpp") -Raw
Check "a scrape ERROR is not cached as an answer" ($dl -match "cacheable=\*/false")
Check "a successful scrape IS cached" ($dl -match "cacheable=\*/true")
Check "the thumb cache is only written when cacheable" ($dl -match "if \(cacheable\)\s*\r?\n\s*m_thumbCache\.insert")
# NOTE: the C++ half above is pinned by CONTRACT, not by execution. Giving it a
# real harness needs a new target in native/CMakeLists.txt, which is a shared
# file. The QML half (retry after an empty answer) IS executed, in the offscreen
# harness below.

# 3. OFFSCREEN BEHAVIOUR
$qmlExe = "C:/Qt/6.11.1/msvc2022_64/bin/qml.exe"
if (-not (Test-Path $qmlExe)) { Write-Host "FAIL  qml.exe not found at $qmlExe"; exit 1 }
$harness = Join-Path $PSScriptRoot "manga_volume_cover_harness.qml"

$prev = $ErrorActionPreference
$ErrorActionPreference = "Continue"
& $qmlExe -platform offscreen $harness 2>&1 | Out-Null
$code = $LASTEXITCODE
$ErrorActionPreference = $prev

Check "offscreen volume-cover contracts pass" ($code -eq 0)

if ($fail -gt 0) { Write-Host "$fail FAILURES"; exit 1 }
Write-Host "manga volume covers: OK"
exit 0
