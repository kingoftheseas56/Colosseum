$ErrorActionPreference = "Stop"

$qmlExe = "C:/Qt/6.11.1/msvc2022_64/bin/qml.exe"
if (!(Test-Path -LiteralPath $qmlExe)) {
    Write-Host "FAIL: qml.exe not found at $qmlExe"
    exit 1
}

$env:QT_FORCE_STDERR_LOGGING = "1"
$harness = Join-Path $PSScriptRoot "comicreader_sync_resume_shell_acceptance.qml"
if (!(Test-Path -LiteralPath $harness)) {
    Write-Host "FAIL: shell imported-resume harness not found at $harness"
    exit 1
}

$previous = $ErrorActionPreference
$ErrorActionPreference = "Continue"
$output = & $qmlExe -platform offscreen $harness 2>&1 | Out-String
$code = $LASTEXITCODE
$ErrorActionPreference = $previous

if ($code -ne 0 -or ($output -notmatch "COMICREADER_SYNC_RESUME_SHELL_OK")) {
    Write-Host "FAIL: ComicReader shell imported-resume gate (exit $code)"
    Write-Host $output
    exit 1
}

Write-Host "COMICREADER_SYNC_RESUME_SHELL_OK"
