$ErrorActionPreference = "Stop"
$exe = Join-Path $PSScriptRoot "..\release\win-unpacked\Colosseum Harness.exe"
$exe = [System.IO.Path]::GetFullPath($exe)
if (-not (Test-Path -LiteralPath $exe)) {
    throw "Packaged executable not found: $exe"
}
$screenshot = Join-Path $env:TEMP "colosseum-harness-packaged-smoke.png"
Remove-Item -LiteralPath $screenshot -Force -ErrorAction SilentlyContinue
$process = Start-Process -FilePath $exe -ArgumentList @("--smoke-test", "--smoke-screenshot=$screenshot") -Wait -PassThru
if ($process.ExitCode -ne 0) {
    throw "Packaged smoke failed with exit code $($process.ExitCode)"
}
if (-not (Test-Path -LiteralPath $screenshot) -or (Get-Item -LiteralPath $screenshot).Length -lt 1024) {
    throw "Packaged smoke did not produce a valid renderer screenshot"
}
Write-Output "PACKAGED_SMOKE_OK"
Write-Output "SCREENSHOT=$screenshot"
