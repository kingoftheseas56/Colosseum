# Orchestration seam contract (Task 14). Player 2 asks the host (Player2HostServices) for episodes,
# sources, subtitles, skip segments, downloads, metadata and progress, and renders whatever it
# resolves — it must NEVER reach a production catalogue / stream source / download store / net client
# itself, in EITHER the engine (native/player2) or the chrome (qml/player2). This is what keeps Player 2
# swappable behind any host. Grep-based (shape, not behaviour); comments count too, so the boundary
# reads clean end to end.
# Run: powershell -NoProfile -File tests/player2/player2_orchestration_contract.ps1

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$roots = @(
    (Join-Path $root 'native/player2'),
    (Join-Path $root 'qml/player2')
)

# Production surfaces the isolated player must not name. The host owns all of these.
$forbidden = @(
    'Cinemeta', 'Torrentio', 'TheatreApi', 'StremioService', 'AddonClient',
    'DownloadStore', 'MangaReader', 'PlayerPage', '\bMpvItem\b', 'XMLHttpRequest'
)

$files = Get-ChildItem -Path $roots -Recurse -Include *.cpp,*.h,*.qml,*.js -File |
    Where-Object { $_.FullName -notmatch '[\\/]build-player2' }
$violations = @()
foreach ($file in $files) {
    $text = Get-Content -Raw $file.FullName
    foreach ($pattern in $forbidden) {
        if ($text -match $pattern) {
            $violations += "$($file.Name): names production surface '$pattern' (the host owns it, not Player 2)"
        }
    }
}

# The seam itself must exist — the one door the player talks to.
if (-not (Test-Path (Join-Path $root 'native/player2/host/Player2HostServices.h'))) {
    $violations += 'Player2HostServices.h missing: the orchestration seam is the only allowed door'
}

if ($violations.Count -gt 0) {
    $violations | ForEach-Object { Write-Error $_ -ErrorAction Continue }
    throw "player2_orchestration_contract: FAIL ($($violations.Count) violation(s))"
}
Write-Output "player2_orchestration_contract: PASS ($($files.Count) player2 files, host-seam doctrine upheld)"
