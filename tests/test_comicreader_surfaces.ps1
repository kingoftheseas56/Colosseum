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
