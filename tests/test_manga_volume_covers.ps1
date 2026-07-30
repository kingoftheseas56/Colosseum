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
