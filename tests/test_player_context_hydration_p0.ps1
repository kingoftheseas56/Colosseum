$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$qmlExe = "C:\Qt\6.11.1\msvc2022_64\bin\qml.exe"
if (!(Test-Path $qmlExe)) { throw "qml.exe not found at $qmlExe - update the Qt path in this test." }

# 1) Behavior: the pure hydration logic (EpisodeBrowser.js) proven headless.
# Verdict rides the exit code (Qt.exit(0) pass / non-zero fail); console output is not
# guaranteed to flush before exit, so the exit code is the source of truth.
$harness = Join-Path $PSScriptRoot "context_hydration_harness.qml"
$out = cmd /c "`"$qmlExe`" -platform offscreen `"$harness`" 2>&1" | Out-String
if ($LASTEXITCODE -ne 0) { throw "context hydration harness failed (exit $LASTEXITCODE):`n$out" }

# 2) Shape: the PlayerPage wiring needles (grep contracts prove PRESENCE, the harness
# above proves the logic behaves; eyes-on proves the pixels).
$player = Get-Content (Join-Path $root "qml\PlayerPage.qml") -Raw
$needles = @(
    'function maybeHydrateContext',      # the hydration entry point exists
    'TheatreApi.loadMeta',               # bare doors refetch the season queue
    'queueContextFromMeta',              # meta -> queue via the pure store
    'mergeHydratedCandidates',           # fetched sources merged around the playing one
    'hydrateGen'                         # stale-callback guard
)
foreach ($n in $needles) {
    if ($player -notmatch [regex]::Escape($n)) { throw "PlayerPage.qml missing hydration wiring: $n" }
}
# Both bare doors must hydrate: playTorrent (Continue Watching resume) and
# playLocalFile (downloaded episodes).
$mTorrent = [regex]::Match($player, '(?s)function playTorrent\(.*?\n    \}')
if (-not $mTorrent.Success -or $mTorrent.Value -notmatch 'maybeHydrateContext') {
    throw "playTorrent must call maybeHydrateContext (Continue Watching door)"
}
$mLocal = [regex]::Match($player, '(?s)function playLocalFile\(.*?\n    \}')
if (-not $mLocal.Success -or $mLocal.Value -notmatch 'maybeHydrateContext') {
    throw "playLocalFile must call maybeHydrateContext (downloaded-episode door)"
}

Write-Host "Player context hydration checks passed."
