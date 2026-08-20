# Catalogue-independence Slice 5 runtime gate (closing sweep, 2026-08-20): proves
# TankobanChapterMigration::run() (native/engine/TankobanChapterMigration.cpp, wired at
# native/main.cpp:1556) actually purges a WC-era chapter tree from a REAL isolated
# colosseum.exe boot, not just the Qt Test fixture (tests/auto/tankoban/
# tst_tankoban_chapter_migration.cpp, already green in the unit gate).
#
# Shape: lanista session run against tests/lanista_scenarios/tankoban_chapter_migration.json,
# seeded from tests/lanista_fixtures/tankoban-chapter-migration-v1 (a manga/ chapter tree +
# a manga-volumes/ archive + a progress-store.ini carrying one manga/tankoban/comic record
# each). The in-session steps only prove the app boots clean and the catalogue-fed Berserk
# masthead still resolves post-migration -- the bridge has no absence assertions (ledger
# limit), so the actual purge verdict is this runner's OWN disk check, performed AFTER the
# session stops, against the tagged AppData root the app itself resolved to and its
# colosseum.log summary line.
#
# AppData isolation: mirrors tests/test_comic_torrent_pack_dltest.ps1 -- Qt's Windows
# QStandardPaths backend resolves AppDataLocation from the registry, and lanista's own
# COLOSSEUM_APPDATA_TAG hook (native/tools/lanista.cpp ~line 300-305) always re-roots the
# tagged session to <Roaming>/Brotherhood/Colosseum-dltest-<tag>, computed here the same way
# so the seed lands where the app actually looks and the post-run disk check reads the same
# root the app actually wrote.
#
# ASCII-only on purpose: a non-ASCII byte in a BOM-less .ps1 is mis-decoded by Windows
# PowerShell (CP1252) and can turn into a smart-quote string delimiter.

$ErrorActionPreference = "Stop"

$root      = Split-Path -Parent $PSScriptRoot
$buildDir  = Join-Path $root "native/build-msvc"
$lanista   = Join-Path $buildDir "lanista.exe"
$appExe    = Join-Path $buildDir "colosseum.exe"
$scenario  = Join-Path $root "tests/lanista_scenarios/tankoban_chapter_migration.json"
$seedDir   = Join-Path $root "tests/lanista_fixtures/tankoban-chapter-migration-v1"

if (!(Test-Path -LiteralPath $lanista)) {
    Write-Host "FAIL: missing $lanista - build native first (lanista)"
    exit 1
}
if (!(Test-Path -LiteralPath $appExe)) {
    Write-Host "FAIL: missing $appExe - build native first (colosseum)"
    exit 1
}
if (!(Test-Path -LiteralPath $scenario)) {
    Write-Host "FAIL: missing scenario $scenario"
    exit 1
}
if (!(Test-Path -LiteralPath $seedDir)) {
    Write-Host "FAIL: missing seed fixture $seedDir"
    exit 1
}

$realAppData = $env:APPDATA
$roamingRoot = Join-Path $realAppData "Brotherhood"
$runStamp    = Get-Date -Format "yyyyMMdd-HHmmss"
$tag         = "tankoban-chmig-$runStamp"
$appDataRoot = Join-Path $roamingRoot ("Colosseum-dltest-" + $tag)

# Pre-clean: this tag is timestamp-unique, but guard against a leftover from an aborted
# earlier attempt at the same second (or a stale manual run) polluting the seed copy.
Remove-Item -LiteralPath $appDataRoot -Recurse -Force -ErrorAction SilentlyContinue

Push-Location $root
try {
    $output = & $lanista --verbose session run $scenario --exe $appExe --qml "qml/Main.qml" `
        --tag $tag --drive --seed $seedDir --ready-ms 60000 2>&1 | Out-String
    $code = $LASTEXITCODE
}
finally {
    Pop-Location
}

$sessionArtifactDir = $null
$m = [regex]::Match($output, "artifacts[\\/]lanista-sessions[\\/](\d{8}-\d{6}-[0-9a-f]{8})")
if ($m.Success) {
    $sessionArtifactDir = Join-Path $root ("artifacts/lanista-sessions/" + $m.Groups[1].Value)
}

if ($code -ne 0) {
    Write-Host "FAIL: lanista session run exited $code"
    Write-Host $output
    exit 1
}
if ($output -match "ISOLATION FAILED") {
    Write-Host "FAIL: session isolation proof failed"
    Write-Host $output
    exit 1
}
if ($output -match "(?m)^FAIL  ") {
    Write-Host "FAIL: at least one scenario step failed"
    Write-Host $output
    exit 1
}

Write-Host "  lanista session ran clean (tag $tag, exit 0)"
if ($sessionArtifactDir) {
    Write-Host "  session artifacts: $sessionArtifactDir"
}

# ---- The runner-side disk verdict (the actual gate) -----------------------------------
$failures = @()

if (!(Test-Path -LiteralPath $appDataRoot)) {
    Write-Host "FAIL: expected tagged AppData root not found: $appDataRoot"
    exit 1
}

$mangaDir = Join-Path $appDataRoot "manga"
if (Test-Path -LiteralPath $mangaDir) {
    $failures += "manga/ chapter tree still present at $mangaDir (migration did not purge it)"
} else {
    Write-Host "  manga/ chapter tree: gone (purged)"
}

$volArchive = Join-Path $appDataRoot "manga-volumes/berserk-1/1.cbz"
if (!(Test-Path -LiteralPath $volArchive)) {
    $failures += "manga-volumes/berserk-1/1.cbz missing (migration touched a different subsystem's storage)"
} else {
    Write-Host "  manga-volumes/ archive: intact"
}

$marker = Join-Path $appDataRoot "tankoban-chapter-migration.v1.done"
if (!(Test-Path -LiteralPath $marker)) {
    $failures += "marker file missing: $marker (migration did not record success)"
} else {
    Write-Host "  migration marker: written"
}

$logPath = Join-Path $appDataRoot "logs/colosseum.log"
if (!(Test-Path -LiteralPath $logPath)) {
    $failures += "colosseum.log not found at $logPath"
} else {
    $logText = Get-Content -LiteralPath $logPath -Raw
    if ($logText -notmatch "\[tankoban-migration\] chapter store purge complete") {
        $failures += "colosseum.log has no migration summary line"
    } elseif ($logText -notmatch "existed=yes deleted=yes") {
        $failures += "migration summary line does not report existed=yes deleted=yes"
    } elseif ($logText -notmatch "1 series dir\(s\)") {
        $failures += "migration summary line does not report 1 series dir(s) purged"
    } elseif ($logText -notmatch "1 manga-kind progress record\(s\) purged") {
        $failures += "migration summary line does not report 1 manga-kind progress record(s) purged"
    } else {
        Write-Host "  colosseum.log summary line: present and matches the seeded fixture (1 series dir, 1 progress record)"
    }
}

# ---- The rebind-gap records check (closing-sweep fix, 2026-08-21) ---------------------
# The colosseum.log summary line above only proves the migration's LAST completed pass
# purged 1 manga-kind record from WHATEVER store it was holding at the time -- it does not
# by itself prove that store is the one QML's `Progress` stays bound to after the session's
# own onboarding step ("continue without an account", scripted in the scenario). This is
# the exact gap the closing sweep (2026-08-21) ground-truthed: the migration used to purge
# ProfileStoreRuntime's throwaway Sealed placeholder (a temp-dir store nothing ever reads
# again) and burn its once-only marker there, leaving the REAL, durable store's manga-kind
# record on disk forever. Read the durable store's own ini directly and assert the record
# is actually gone from it. In THIS scenario (a fresh, never-adopted tagged session)
# FirstAccountProfileCoordinator::prepareLocalOnly() takes the reloadLegacyProfile() branch
# (legacyPersonalStateClaimed() is false pre-adoption), whose ProgressStore is the
# COLOSSEUM_APPDATA_TAG-diverted default constructor -- the SAME path as the seed fixture's
# own file, $appDataRoot/progress-store.ini (ProgressStore.h's progressStoreTaggedIniPath).
# An already-adopted profile would instead land in profiles/local/progress.ini
# (ProfilePaths::localOnly) -- checked too, defensively, in case that branch is ever the
# one exercised here.
$durableIniCandidates = @(
    (Join-Path $appDataRoot "progress-store.ini"),
    (Join-Path $appDataRoot "profiles/local/progress.ini")
)
$durableIniChecked = $false
foreach ($iniPath in $durableIniCandidates) {
    if (!(Test-Path -LiteralPath $iniPath)) { continue }
    $durableIniChecked = $true
    # Matched by the fixture's own id VALUES, not the "kind" JSON key -- the disk-writer's
    # own QSettings ini-escaping of the backslash/quote-heavy JSON blob is a layer this
    # script does not need to reproduce exactly; each seeded id value is unique to its one
    # record (berserk-1 only ever appears in the manga-kind row; mal:2 only in the
    # tankoban-kind row; locg:123 only in the comic-kind row), so a plain substring check
    # is exact without needing to know the on-disk escaping shape.
    $iniText = Get-Content -LiteralPath $iniPath -Raw
    if ($iniText.Contains("berserk-1")) {
        $failures += "the durable store's own ini ($iniPath) still contains the manga-kind berserk-1 record -- the rebind gap this check exists to catch"
    } else {
        Write-Host "  durable store ini ($iniPath): manga-kind record confirmed absent"
    }
    if (!$iniText.Contains("mal:2")) {
        $failures += "the durable store's own ini ($iniPath) lost its tankoban-kind record (over-purge)"
    }
    if (!$iniText.Contains("locg:123")) {
        $failures += "the durable store's own ini ($iniPath) lost its comic-kind record (over-purge)"
    }
}
if (!$durableIniChecked) {
    $failures += "neither durable-profile ini candidate exists under $appDataRoot -- cannot confirm the rebind purge reached the real store"
}

if ($failures.Count -gt 0) {
    Write-Host ("FAIL: tankoban chapter migration disk gate - " + ($failures -join "; "))
    Write-Host "AppData root left on disk for inspection: $appDataRoot"
    exit 1
}

Write-Host "TANKOBAN_CHAPTER_MIGRATION_OK"
exit 0
