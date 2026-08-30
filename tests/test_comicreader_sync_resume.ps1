# Comic Reader — imported Tankoban resume bridge gate (Bundle 8B).

$ErrorActionPreference = "Stop"

$qmlExe = "C:/Qt/6.11.1/msvc2022_64/bin/qml.exe"
if (!(Test-Path -LiteralPath $qmlExe)) {
    Write-Host "FAIL: qml.exe not found at $qmlExe"
    exit 1
}

$env:QT_FORCE_STDERR_LOGGING = "1"
$harness = Join-Path $PSScriptRoot "comicreader_sync_resume_acceptance.qml"
if (!(Test-Path -LiteralPath $harness)) {
    Write-Host "FAIL: imported-resume harness not found at $harness"
    exit 1
}

$bridge = Join-Path $PSScriptRoot "../qml/comicreader/ComicReaderSyncedResumeBridge.qml"
$resolver = Join-Path $PSScriptRoot "../qml/comicreader/ComicReaderImportedResume.js"
foreach ($path in @($bridge, $resolver)) {
    if (!(Test-Path -LiteralPath $path)) {
        Write-Host "FAIL: Bundle 8B reader sync source not found at $path"
        exit 1
    }
}

$previous = $ErrorActionPreference
$ErrorActionPreference = "Continue"
$output = & $qmlExe -platform offscreen $harness 2>&1 | Out-String
$code = $LASTEXITCODE
$ErrorActionPreference = $previous

if ($code -ne 0 -or ($output -notmatch "COMICREADER_SYNC_RESUME_OK")) {
    Write-Host "FAIL: ComicReader imported-resume bridge gate (exit $code)"
    Write-Host $output
    exit 1
}

Write-Host "COMICREADER_SYNC_RESUME_OK"
