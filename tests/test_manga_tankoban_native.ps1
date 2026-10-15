# Native deterministic suite for Tankoban "volume mode" (Task 11).
#
# Runs the native C++ harnesses that prove the volume-mode organs OFFLINE - no
# live Nyaa, no WeebCentral, no real BitTorrent. Each must print its *_OK
# sentinel and exit 0. Qt's runtime bin is prepended to PATH so every harness
# resolves Qt6Core.dll etc.
#
# The live end-to-end gate (a REAL download proven by "[tankoban-dltest] DONE")
# runs ONLY when COLOSSEUM_TANKOBAN_DLTEST is set - Hemanth's call. Deterministic
# / CI runs never set it, so this file stays fully offline by default.
#
# ASCII-only on purpose: a non-ASCII byte in a BOM-less .ps1 is mis-decoded by
# Windows PowerShell (CP1252) and can turn into a smart-quote string delimiter.

$ErrorActionPreference = "Stop"

$root     = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $root "native/build-msvc"
$qtBin    = "C:/Qt/6.11.1/msvc2022_64/bin"

# Qt runtime DLLs on PATH so each harness exe resolves its Qt dependencies.
$env:PATH = "$qtBin;$env:PATH"

$harnesses = @(
    @{ Exe = "manga_tankoban_logic_harness.exe";   Sentinel = "MANGA_TANKOBAN_LOGIC_OK" },
    @{ Exe = "manga_volume_identity_harness.exe";  Sentinel = "MANGA_VOLUME_IDENTITY_OK" },
    @{ Exe = "manga_volume_filepicker_harness.exe"; Sentinel = "MANGA_VOLUME_FILEPICKER_OK" },
    @{ Exe = "manga_volume_index_harness.exe";      Sentinel = "MANGA_VOLUME_INDEX_OK" },
    @{ Exe = "manga_volume_torrent_harness.exe";    Sentinel = "MANGA_VOLUME_TORRENT_OK" },
    @{ Exe = "manga_volume_packer_harness.exe";     Sentinel = "MANGA_VOLUME_PACKER_OK" },
    @{ Exe = "manga_tankoban_service_harness.exe";  Sentinel = "MANGA_TANKOBAN_SERVICE_OK" },
    @{ Exe = "manga_torrent_metainfo_resolver_harness.exe"; Sentinel = "MANGA_TORRENT_METAINFO_RESOLVER_OK" },
    @{ Exe = "manga_torrent_index_harness.exe";     Sentinel = "MANGA_TORRENT_INDEX_OK" },
    @{ Exe = "manga_torrent_indexer_harness.exe";   Sentinel = "MANGA_TORRENT_INDEXER_OK" }
)

foreach ($h in $harnesses) {
    $exe = Join-Path $buildDir $h.Exe
    if (!(Test-Path -LiteralPath $exe)) {
        Write-Host "FAIL: missing harness $($h.Exe) - build native first"
        exit 1
    }
    # A harness may print benign warnings on stderr; don't let a stderr line
    # terminate before we read the verdict + exit code.
    $prevEAP = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $output = & $exe 2>&1 | Out-String
    $code = $LASTEXITCODE
    $ErrorActionPreference = $prevEAP
    if ($code -ne 0 -or ($output -notmatch [regex]::Escape($h.Sentinel))) {
        Write-Host "FAIL: $($h.Exe) (exit $code, sentinel $($h.Sentinel) missing)"
        Write-Host $output
        exit 1
    }
    Write-Host ("  {0} -> {1} (exit 0)" -f $h.Exe, $h.Sentinel)
}

# --- Hemanth's live gate ONLY: one real end-to-end acquisition ---
# Deterministic runs NEVER set COLOSSEUM_TANKOBAN_DLTEST, so this stays skipped.
if ($env:COLOSSEUM_TANKOBAN_DLTEST) {
    $exe = Join-Path $buildDir "colosseum.exe"
    if (!(Test-Path -LiteralPath $exe)) {
        Write-Host "FAIL: colosseum.exe not found for the live DLTEST gate"
        exit 1
    }
    $env:QML_DISABLE_DISK_CACHE  = "1"
    $env:QT_FORCE_STDERR_LOGGING = "1"
    $prevEAP = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    Push-Location $root   # cwd = repo root so 'qml/Main.qml' resolves
    $output = & $exe "qml/Main.qml" 2>&1 | Out-String
    $code = $LASTEXITCODE
    Pop-Location
    $ErrorActionPreference = $prevEAP
    if ($output -notmatch "\[tankoban-dltest\] DONE") {
        Write-Host "FAIL: live DLTEST did not reach [tankoban-dltest] DONE (exit $code)"
        Write-Host $output
        exit 1
    }
    Write-Host ("  live DLTEST -> [tankoban-dltest] DONE (exit {0})" -f $code)
}

Write-Host "manga tankoban native: OK"
exit 0
