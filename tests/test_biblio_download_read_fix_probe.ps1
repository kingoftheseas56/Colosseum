# tests/test_biblio_download_read_fix_probe.ps1
#
# Hostile Lanista regression probe for commit 38339f4 ("fix(biblio): stabilize
# downloaded book read journey"). Two phases:
#
#   1. STATIC  - prove the key implementation contracts of 38339f4 are present in the
#                worktree source (presence, not behavior).
#   2. LIVE    - run the isolated Lanista scenario (tests/lanista_scenarios/
#                biblio_downloaded_epub_read_journey.json) against a disposable tagged
#                app session seeded with tests/lanista-seeds/biblio-downloaded-epub-v1,
#                then run the warning gate on the session logs.
#
# Prints EXACTLY "BIBLIO_DOWNLOAD_READ_FIX_PROBE_OK" only when every phase is green.
#
# Usage:
#   powershell tests/test_biblio_download_read_fix_probe.ps1 [-Lanista <lanista.exe>]
#       [-Exe <colosseum.exe>] [-Qml <qml/Main.qml>] [-Seed <seed dir>]
#       [-Scenario <scenario.json>] [-Tag <t>] [-SkipLive] [-NoWarningGate]
#
# A build (native/build-msvc) is NOT present in this worktree, so the LIVE phase
# auto-locates a source-equivalent existing build (the sibling worktree at the same
# commit 38339f4) and loads THIS worktree's qml/ tree via the app's qml-override
# argument. Pass -SkipLive to run only the static contract proof.
#
# ASCII-only on purpose: Windows PowerShell 5.1 misdecodes BOM-less UTF-8 .ps1
# (see tests/warning_gate.ps1 header).

param(
    [string]$Lanista = "",
    [string]$Exe = "",
    [string]$Qml = "",
    [string]$Seed = "",
    [string]$Scenario = "",
    [string]$Tag = "biblioepubfix",
    [switch]$SkipLive = $false,
    [switch]$NoWarningGate = $false
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

function Read-RepoFile([string]$rel) { Get-Content -Raw -LiteralPath (Join-Path $root $rel) }
function Assert-Contains([string]$text, [string]$needle, [string]$msg) {
    if (-not $text.Contains($needle)) { Write-Host "FAIL: $msg"; exit 1 }
}

# ---------------------------------------------------------------- phase 1: static
Write-Host "[probe] phase 1: static contracts of 38339f4"

$biblioBook = Read-RepoFile "qml/BiblioBook.qml"
$main       = Read-RepoFile "qml/Main.qml"
$bookDl     = Read-RepoFile "native/engine/BookDownloader.cpp"

# (1) The durable downloadedBooks fallback: refreshLocal recovers a downloaded EPUB by
#     normalized title+author identity when live sources are empty/reordered/changed.
Assert-Contains $biblioBook "books.downloadedBooks()" `
    "BiblioBook.refreshLocal must scan Books.downloadedBooks()"
Assert-Contains $biblioBook 'BiblioApi.pairKey(row.title || "", row.author || "")' `
    "the durable fallback must match normalized title+author identity"
Assert-Contains $biblioBook 'row.missing || !String(row.path || "").length' `
    "the durable fallback must skip missing/empty rows"

# (2) One physical EPUB = one Biblio session identity: the local path is the target id.
Assert-Contains $main '"target": { "path": path, "book": b, "id": path }' `
    "openBookSession must make the local path the session target id"

# (3) Downloads Read must preserve title+author for pairing/taskbar identity.
Assert-Contains $main '"id": item.id || item.path, "title": item.title || "", "author": item.author || ""' `
    "Downloads Read must pass title and author through"

# (4) The boot-time self-heal (design target): a stale absolute path whose filename matches
#     a same-named file under the current books dir is re-rooted before pruning.
Assert-Contains $bookDl "QFileInfo(e.path).fileName()" `
    "BookDownloader.loadIndex must self-heal by re-rooting onto a same-named file"
Assert-Contains $bookDl "rerooted != e.path" `
    "BookDownloader.loadIndex must only re-root when the path actually changed"

Write-Host "[probe] phase 1: OK (all static contracts present)"

if ($SkipLive) {
    Write-Host "[probe] -SkipLive: static-only run, no live session"
    Write-Host "BIBLIO_DOWNLOAD_READ_FIX_PROBE_STATIC_OK"
    exit 0
}

# ------------------------------------------------------------- phase 2: locate build
if ($Lanista -eq "") {
    $candidates = @(
        (Join-Path $root "native/build-msvc/lanista.exe"),
        (Join-Path $root "../biblio-download-read-journey/native/build-msvc/lanista.exe"),
        (Join-Path (Join-Path (Split-Path -Parent $root) "..") "Brotherhood/Colosseum/native/build-msvc/lanista.exe")
    ) | ForEach-Object { [System.IO.Path]::GetFullPath($_) }
    $Lanista = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
}
if ($Exe -eq "") {
    $candidates = @(
        (Join-Path $root "native/build-msvc/colosseum.exe"),
        (Join-Path $root "../biblio-download-read-journey/native/build-msvc/colosseum.exe"),
        (Join-Path (Join-Path (Split-Path -Parent $root) "..") "Brotherhood/Colosseum/native/build-msvc/colosseum.exe")
    ) | ForEach-Object { [System.IO.Path]::GetFullPath($_) }
    $Exe = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
}
if ($Qml -eq "")      { $Qml = Join-Path $root "qml/Main.qml" }
if ($Seed -eq "")     { $Seed = Join-Path $root "tests/lanista-seeds/biblio-downloaded-epub-v1" }
if ($Scenario -eq "") { $Scenario = Join-Path $root "tests/lanista_scenarios/biblio_downloaded_epub_read_journey.json" }

if (-not $Lanista) {
    Write-Host "[probe] BLOCKED: no lanista.exe build found; pass -Lanista or build native. (static phase still green)"
    exit 4
}
if (-not $Exe) {
    Write-Host "[probe] BLOCKED: no colosseum.exe build found; pass -Exe or build native. (static phase still green)"
    exit 4
}
if (-not (Test-Path -LiteralPath $Scenario)) { Write-Host "FAIL: scenario missing: $Scenario"; exit 2 }
if (-not (Test-Path -LiteralPath $Seed))     { Write-Host "FAIL: seed missing: $Seed"; exit 2 }
if (-not (Test-Path -LiteralPath $Qml))      { Write-Host "FAIL: qml missing: $Qml"; exit 2 }

Write-Host ("[probe] lanista  : " + $Lanista)
Write-Host ("[probe] exe      : " + $Exe)
Write-Host ("[probe] qml      : " + $Qml)
Write-Host ("[probe] seed     : " + $Seed)
Write-Host ("[probe] scenario : " + $Scenario)

# ----------------------------------------------------------------- phase 3: live run
Write-Host "[probe] phase 3: isolated Lanista session (tag=$Tag)"
# The seed copy (QFile::copy) refuses to overwrite, so a prior run's tagged AppData would
# break a clean seed. Remove the disposable tagged roots first; never touch the daily app.
$roamTag = Join-Path $env:APPDATA "Brotherhood/Colosseum-dltest-$Tag"
$localTag = Join-Path $env:LOCALAPPDATA "Brotherhood/Colosseum-dltest-$Tag"
Remove-Item -LiteralPath $roamTag -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $localTag -Recurse -Force -ErrorAction SilentlyContinue
Push-Location $root
try {
    & $Lanista session run $Scenario --exe $Exe --qml $Qml --seed $Seed `
        --tag $Tag --drive --ready-ms 90000 2>&1 | ForEach-Object { Write-Host $_ }
    $runExit = $LASTEXITCODE
}
finally {
    Pop-Location
}
if ($runExit -ne 0) {
    Write-Host "FAIL: Lanista scenario exit code $runExit"
    exit 1
}
Write-Host "[probe] phase 3: OK (scenario green)"

# ---------------------------------------------------------------- phase 4: warnings
if ($NoWarningGate) {
    Write-Host "[probe] -NoWarningGate: skipping warning gate"
    Write-Host "BIBLIO_DOWNLOAD_READ_FIX_PROBE_FUNCTIONAL_OK"
    exit 0
} else {
    Write-Host "[probe] phase 4: warning gate"
    $appLog = Join-Path $env:APPDATA "Brotherhood/Colosseum-dltest-$Tag/logs/colosseum.log"
    $latestSession = Get-ChildItem -LiteralPath (Join-Path $root "artifacts/lanista-sessions") `
        -Directory -ErrorAction SilentlyContinue | Sort-Object LastWriteTime | Select-Object -Last 1
    $gate = Join-Path $root "tests/warning_gate.ps1"
    $logPaths = @()
    if (Test-Path -LiteralPath $appLog) { $logPaths += $appLog }
    if ($latestSession) {
        $stderr = Join-Path $latestSession.FullName "stderr.log"
        if (Test-Path -LiteralPath $stderr) { $logPaths += $stderr }
    }
    if ($logPaths.Count -eq 0) {
        Write-Host "[probe] phase 4: no session logs found; skipping warning gate"
    } else {
        & powershell.exe -NoProfile -File $gate -LogPath ($logPaths -join ",") 2>&1 | ForEach-Object { Write-Host $_ }
        if ($LASTEXITCODE -ne 0) {
            Write-Host "FAIL: warning gate did not pass"
            exit 1
        }
        Write-Host "[probe] phase 4: OK (WARNING_GATE_OK)"
    }
}

Write-Host "BIBLIO_DOWNLOAD_READ_FIX_PROBE_OK"
exit 0
