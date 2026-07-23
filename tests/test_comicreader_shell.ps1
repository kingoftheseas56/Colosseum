# Comic Reader — ORCHESTRATION SHELL gate (Task 9).
#
# Drives qml/comicreader/ComicReaderShell.qml offscreen through comicreader_shell_harness.qml and
# asserts the orchestration the shell owns: openEntry with the smart-default direction per lane,
# acquisition routing (downloadChapter / downloadIssue / sourceRequested), resume-before-paint,
# the byte-identical Progress.record payload (fires on page change + close, guarded at max<=0),
# newest-first crossing that records before jumping, close -> core.closeEntry(), and graceful
# degradation when a seam is null.
#
# Plus a STATIC guard: the shell must import NOTHING under guided/ and reference no guided service
# (Guided is frozen and owned elsewhere). PowerShell can read the file directly, which qml.exe
# cannot do reliably — so the "no guided" assertion is a grep here, the behavior is the harness.
#
# qml.exe is located exactly as every sibling tests/test_*.ps1 does (hardcoded Qt path); -I
# tests/qmlmock mirrors the sibling harnesses (harmless here — the shell needs no mock module).

$ErrorActionPreference = "Stop"

$qmlExe = "C:/Qt/6.11.1/msvc2022_64/bin/qml.exe"
if (!(Test-Path -LiteralPath $qmlExe)) {
    Write-Host "FAIL: qml.exe not found at $qmlExe"
    exit 1
}

# --- static assertion: NO guided import/reference anywhere in the shell ---
$shellQml = Join-Path $PSScriptRoot "../qml/comicreader/ComicReaderShell.qml"
if (!(Test-Path -LiteralPath $shellQml)) {
    Write-Host "FAIL: ComicReaderShell.qml not found at $shellQml"
    exit 1
}
$guidedHits = Select-String -LiteralPath $shellQml -Pattern "guided" -SimpleMatch -CaseSensitive:$false
if ($guidedHits) {
    Write-Host "FAIL: ComicReaderShell.qml must contain NO guided reference; found:"
    $guidedHits | ForEach-Object { Write-Host ("  line " + $_.LineNumber + ": " + $_.Line.Trim()) }
    exit 1
}

$env:QT_FORCE_STDERR_LOGGING = "1"
$harness  = Join-Path $PSScriptRoot "comicreader_shell_harness.qml"
$mockPath = Join-Path $PSScriptRoot "qmlmock"

$prevEAP = $ErrorActionPreference
$ErrorActionPreference = "Continue"
$output = & $qmlExe -platform offscreen -I $mockPath $harness 2>&1 | Out-String
$code = $LASTEXITCODE
$ErrorActionPreference = $prevEAP

if ($code -ne 0 -or ($output -notmatch "COMICREADER_SHELL_OK")) {
    Write-Host "FAIL: comic reader shell offscreen harness (exit $code)"
    Write-Host $output
    exit 1
}

Write-Host "COMICREADER_SHELL_OK"
