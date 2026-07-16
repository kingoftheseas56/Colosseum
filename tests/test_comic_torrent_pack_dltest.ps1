# Task 11: real-engine, deterministic, LEGAL gate for the Tankorent Comic
# pack-selection chain (native/torrent/ComicTorrentDownloader.h's "shared-
# infohash EDITION pack transport"). A local loopback libtorrent seeder
# (comic_torrent_pack_seed_harness.exe) holds fixture comic archives - real
# ZIPs of magic-byte-valid tiny images, no external network, no piracy. This
# runner drives the REAL production select->download->assemble->publish
# ->restart chain against it via COLOSSEUM_COMIC_PACK_DLTEST, exactly the way
# COLOSSEUM_TANKOBAN_DLTEST proves the manga volume-mode chain
# (tests/test_manga_tankoban_native.ps1).
#
# AppData isolation: Qt's Windows QStandardPaths backend resolves
# AppDataLocation from the registry (SHGetKnownFolderPath), NOT the
# $env:APPDATA process variable - verified empirically (overriding
# $env:APPDATA left AppDataLocation pointing at the real Roaming folder).
# main.cpp's COLOSSEUM_APPDATA_TAG hook instead suffixes the Qt application
# name for the run, which QStandardPaths DOES honor immediately, redirecting
# every AppData-backed store (comics index, the pack transport's ledger/
# staging/torrent dirs, QSettings, ...) to a disposable sibling folder under
# the SAME Roaming\Brotherhood tree - never the real Roaming\Brotherhood\
# Colosseum tree a brother's actual downloads live in. Each scenario gets its
# own tag (a fresh, pre-cleaned folder); the restart scenario reuses ONE tag
# across its two launches so the second one replays the first one's ledger.
#
# ASCII-only on purpose: a non-ASCII byte in a BOM-less .ps1 is mis-decoded by
# Windows PowerShell (CP1252) and can turn into a smart-quote string delimiter.

$ErrorActionPreference = "Stop"

$root     = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $root "native/build-msvc"
$qtBin    = "C:/Qt/6.11.1/msvc2022_64/bin"
$env:PATH = "$qtBin;$env:PATH"

$seedExe = Join-Path $buildDir "comic_torrent_pack_seed_harness.exe"
$appExe  = Join-Path $buildDir "colosseum.exe"
if (!(Test-Path -LiteralPath $seedExe)) {
    Write-Host "FAIL: missing $seedExe - build native first (comic_torrent_pack_seed_harness)"
    exit 1
}
if (!(Test-Path -LiteralPath $appExe)) {
    Write-Host "FAIL: missing $appExe - build native first (colosseum)"
    exit 1
}

function New-CleanDir([string]$path) {
    Remove-Item -LiteralPath $path -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Path $path -Force | Out-Null
    return $path
}

$realAppData = $env:APPDATA
$roamingRoot = Join-Path $realAppData "Brotherhood"

function AppDataRootFor([string]$tag) {
    return Join-Path $roamingRoot "Colosseum-dltest-$tag"
}

# ── Seed the fixture torrent ────────────────────────────────────────────────
$seedWork = New-CleanDir (Join-Path ([System.IO.Path]::GetTempPath()) "colosseum-comic-pack-seed")
$seedOutLog = Join-Path $seedWork "seed.out.log"
$seedErrLog = Join-Path $seedWork "seed.err.log"
$seedProc = Start-Process -FilePath $seedExe -ArgumentList "`"$seedWork`"" `
    -NoNewWindow -PassThru -RedirectStandardOutput $seedOutLog -RedirectStandardError $seedErrLog

$magnet = $null
$deadline = (Get-Date).AddSeconds(30)
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 300
    if (Test-Path -LiteralPath $seedOutLog) {
        $text = Get-Content -LiteralPath $seedOutLog -Raw -ErrorAction SilentlyContinue
        if ($text -match "READY (magnet:\?\S+)") { $magnet = $Matches[1]; break }
    }
    if ($seedProc.HasExited) { break }
}
if (-not $magnet) {
    Write-Host "FAIL: seed harness never printed a magnet"
    Write-Host (Get-Content -LiteralPath $seedErrLog -Raw -ErrorAction SilentlyContinue)
    if (!$seedProc.HasExited) { Stop-Process -Id $seedProc.Id -Force }
    exit 1
}
Write-Host ("  seed magnet acquired ({0} chars)" -f $magnet.Length)

function Invoke-ScenarioIn([string]$spec, [string]$tag, [string]$appDataRoot) {
    $env:COLOSSEUM_APPDATA_TAG      = $tag
    $env:COLOSSEUM_COMIC_PACK_DLTEST = $spec
    $env:QML_DISABLE_DISK_CACHE      = "1"
    $env:QT_FORCE_STDERR_LOGGING     = "1"
    $prevEAP = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    Push-Location $root
    $output = & $appExe "qml/Main.qml" 2>&1 | Out-String
    $code = $LASTEXITCODE
    Pop-Location
    $ErrorActionPreference = $prevEAP
    Remove-Item Env:\COLOSSEUM_COMIC_PACK_DLTEST -ErrorAction SilentlyContinue
    Remove-Item Env:\COLOSSEUM_APPDATA_TAG -ErrorAction SilentlyContinue
    return @{ Output = $output; Code = $code; AppDataRoot = $appDataRoot }
}

# Fresh-AppData wrapper for the 3 one-shot scenarios (single/issues/shared) -
# each gets a clean, pre-wiped root. NEVER used for the restart scenario's
# SECOND launch: wiping the root there would destroy the very ledger row
# launch 1 left behind for launch 2 to replay (the bug this comment replaces
# - Invoke-Scenario used to clean on every call, so the "restart" launch
# silently replayed nothing and timed out).
function Invoke-Scenario([string]$spec, [string]$tag) {
    $appDataRoot = New-CleanDir (AppDataRootFor $tag)
    return Invoke-ScenarioIn $spec $tag $appDataRoot
}

$failures = @()

# ---- single: Compendium v01 -> exact-title tier, one archive, real pages ----
$r = Invoke-Scenario "single|$magnet|compendium-v01" "single"
if ($r.Code -ne 0 -or $r.Output -notmatch "COMIC_PACK_SINGLE_DONE pages=[1-9][0-9]*") {
    Write-Host "FAIL: single scenario (exit $($r.Code))"
    Write-Host $r.Output
    $failures += "single"
} else {
    Write-Host "  single -> OK"
}

# ---- issues: collected issue-set tier, several archives, reports groups ----
$r = Invoke-Scenario "issues|$magnet|issue-set" "issues"
if ($r.Code -ne 0 -or $r.Output -notmatch "COMIC_PACK_ISSUES_DONE pages=[1-9][0-9]* groups=[1-9][0-9]*") {
    Write-Host "FAIL: issues scenario (exit $($r.Code))"
    Write-Host $r.Output
    $failures += "issues"
} else {
    Write-Host "  issues -> OK"
}

# ---- shared: two editions share one infohash; cancel the first after     ----
# ---- progress, the second still finishes                                 ----
$r = Invoke-Scenario "shared|$magnet|compendium-v01|compendium-v02" "shared"
if ($r.Code -ne 0 -or $r.Output -notmatch "COMIC_PACK_SHARED_DONE pages=[1-9][0-9]*") {
    Write-Host "FAIL: shared scenario (exit $($r.Code))"
    Write-Host $r.Output
    $failures += "shared"
} else {
    Write-Host "  shared -> OK"
}

# ---- restart: launch1 (single) killed mid-flight BY PID, launch2 replays ----
# ---- the SAME ledger from the SAME AppData root                          ----
$restartTag = "restart"
$restartRoot = New-CleanDir (AppDataRootFor $restartTag)
$env:COLOSSEUM_APPDATA_TAG       = $restartTag
$env:COLOSSEUM_COMIC_PACK_DLTEST = "single|$magnet|compendium-v01"
$env:QML_DISABLE_DISK_CACHE      = "1"
$env:QT_FORCE_STDERR_LOGGING     = "1"
$launch1OutLog = Join-Path $seedWork "restart-launch1.out.log"
$launch1ErrLog = Join-Path $seedWork "restart-launch1.err.log"
Push-Location $root
$launch1 = Start-Process -FilePath $appExe -ArgumentList "qml/Main.qml" -PassThru -NoNewWindow `
    -RedirectStandardOutput $launch1OutLog -RedirectStandardError $launch1ErrLog
Pop-Location

$ledgerPath = Join-Path $restartRoot "comics-torrent/edition-requests.json"
$sawDownloading = $false
# A tiny loopback download races through awaiting_metadata -> downloading ->
# completed in well under the old 250ms poll gap, so waiting to catch the
# transient "downloading" state alone was flaky (it silently missed the window
# and bailed). Poll tightly and catch ANY non-terminal (resumable) state: the
# ledger writes a valid-infoHash row at awaiting_metadata BEFORE the download,
# and active() replays every non-terminal row, so killing launch 1 at ANY of
# these stages leaves exactly the interrupted request launch 2 must resume.
$liveStates = @("awaiting_metadata", "downloading", "assembling", "publishing")
$deadline = (Get-Date).AddSeconds(60)
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 25
    if ($launch1.HasExited) { break }   # finished (or died) before we could catch it mid-flight
    if (Test-Path -LiteralPath $ledgerPath) {
        try {
            $json = Get-Content -LiteralPath $ledgerPath -Raw -ErrorAction Stop | ConvertFrom-Json
            $rows = @($json.rows)
            if ($rows | Where-Object { $liveStates -contains $_.state }) { $sawDownloading = $true; break }
        } catch { }
    }
}
if ($sawDownloading -and -not $launch1.HasExited) {
    Stop-Process -Id $launch1.Id -Force   # kill ONLY this PID, never by image name
    Start-Sleep -Milliseconds 500
}
Remove-Item Env:\COLOSSEUM_COMIC_PACK_DLTEST -ErrorAction SilentlyContinue
Remove-Item Env:\COLOSSEUM_APPDATA_TAG -ErrorAction SilentlyContinue

if (-not $sawDownloading) {
    Write-Host "FAIL: restart scenario never observed a resumable (non-terminal) ledger row before launch 1 finished"
    Write-Host (Get-Content -LiteralPath $launch1OutLog -Raw -ErrorAction SilentlyContinue)
    Write-Host (Get-Content -LiteralPath $launch1ErrLog -Raw -ErrorAction SilentlyContinue)
    $failures += "restart"
} else {
    Write-Host "  restart: launch 1 killed mid-flight (PID $($launch1.Id)) with a resumable (non-terminal) ledger row"
    # Invoke-ScenarioIn (NOT Invoke-Scenario) - launch 2 must land in the SAME
    # AppData root launch 1 just wrote to, untouched, so the ledger replay has
    # something to resume.
    $r = Invoke-ScenarioIn "restart|$magnet|compendium-v01" $restartTag $restartRoot
    if ($r.Code -ne 0 -or $r.Output -notmatch "COMIC_PACK_RESTART_DONE pages=[1-9][0-9]* records=1") {
        Write-Host "FAIL: restart scenario replay (exit $($r.Code))"
        Write-Host $r.Output
        $failures += "restart"
    } else {
        Write-Host "  restart -> OK"
    }
}

# ── Teardown the seeder ─────────────────────────────────────────────────────
if (!$seedProc.HasExited) { Stop-Process -Id $seedProc.Id -Force }

if ($failures.Count -gt 0) {
    Write-Host ("FAIL: comic torrent pack dltest - scenarios failed: " + ($failures -join ", "))
    exit 1
}

Write-Host "comic torrent pack dltest: OK"
exit 0
